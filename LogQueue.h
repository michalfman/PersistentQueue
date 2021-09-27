
#ifndef LOG_QUEUE_H_
#define LOG_QUEUE_H_

#include <atomic>
#include "Utilities.h"

//=============================Start LogQueue Class==========================//
/* This queue preserves the durable linearizability and detectable execution
 * definitions. This version does NOT contain memory management by Hazard
 * Pointers. Every operation is sent with an operation number and saved within a
 * log array. Every thread has its entrance in the array, and upon recovery it
 * can tell whether the operation was executed on the queue or not.
 */
template <class T> class LogQueue {
  public:

    class NodeWithLog;
    class LogEntry;

    // Indicated which operation the user is trying to execute.
    enum Action {none, insert, remove};

    //=======================Start NodeWithLog Class=========================//
    /* NodeWithLog is the type of the elements that will be in the queue.
     * It contains the following fields:
     * value     - can be of any type. It holds the data of the element.
     * next      - a pointer to the next element in the queue.
     * logEnq    - a pointer to a LogEntry that holds the log of the insertion
     * 	           of that specific node.
     * logDeq    - a pointer to a LogEntry that holds the log of the removal
     * 		   of that specific node (if exists).
     */
    class NodeWithLog {
      public:
	T value;
        std::atomic<NodeWithLog*> next;
        std::atomic<LogEntry*> logEnq;
        std::atomic<LogEntry*> logDeq;
        NodeWithLog(T val) : next(nullptr), logEnq(nullptr),
                             logDeq(nullptr), value(val) {}
        NodeWithLog() : next(nullptr), logEnq(nullptr),
                        logDeq(nullptr), value(T()) {}
    };
    //=========================End NodeWithLog Class=========================//

    //=========================Start LogEntry Class==========================//
    /* LogEntry is the type of the elements that will be in the logs array.
     * This entry represents an operation. It contains the following fields:
     * operationNum - The number of the operation that is given by the user.
     * 		      Helps to track which operations were executed.
     * action       - The operation that was asked by the user - insert/remove.
     * status       - updated ONLY if the queue is empty and the thread wants
     * 		      to remove a node from an empty queue. Updated a moment
     * 		      before the thread returns.
     * logEnq       - a pointer to a LogEntry that holds the log of the
     *  	      insertion of that specific node.
     * logEnq       - a pointer to a LogEntry that holds the log of the removal
     * 		      of that specific node (if exists).
     */
    class LogEntry {
      public:
	int operationNum;
	Action action;
	bool status;
        NodeWithLog* node;
	LogEntry(): operationNum(-1), action(none), status(false),
		    node(NULL) {}
	LogEntry(bool s, NodeWithLog* n, Action a, int operationNumber):
		operationNum(operationNumber), action(a), status(s),
		node(n) {}
    };
    //==========================End LogEntry Class===========================//

    // The LogEntry array. Each thread has an entrance where is saves the last
    // operation that was asked by the user.
    LogEntry* logs[MAX_THREADS * PADDING];

    /* The constructor of the queue. Makes the head and tail point to a durable
     * dummy node. Updating the initial data(snapshot) to point to that node as
     * well.
     */
    LogQueue() {
	NodeWithLog* dummy = new NodeWithLog(INT_MAX);
	BARRIER(dummy);  // Flush the dummy node before connecting it
	head = tail = dummy;
	BARRIER(&head);
	BARRIER(&tail);
	for (int i = 0; i < MAX_THREADS; i++) {
	    logs[i * PADDING] = nullptr;
	    BARRIER(&logs[i * PADDING]);
	}
    }
    //-------------------------------------------------------------------------
    
    void initialize(){
        for (int i = 0; i < QUEUE_SIZE; i++){
            enq(i+1, 0, -1);
        }
    }
    //-------------------------------------------------------------------------

    /* Enqueues a node to the queue with the given value. */
    void enq(T value, int threadID, int operationNumber) {
	NodeWithLog* node = createEnqLogAndNode(value, threadID,
                                                operationNumber);
	while (true) {
      	    NodeWithLog* last = tail.load();
       	    NodeWithLog* next = last->next.load();
	    if (last == tail.load()) {
		if (next == nullptr) {
                    // Try to insert.
                    if (last->next.compare_exchange_strong(next, node)) {
                        BARRIER_OPT(&last->next);
                        tail.compare_exchange_strong(last, node);
        		return;
		    }
		} else {  // If next is a node, help concurrent operation
                    BARRIER_OPT(&last->next);
                    tail.compare_exchange_strong(last, next);
                }
	    }
	}
    }
    //-------------------------------------------------------------------------

    /* Tries to dequeue a node. Returns the value of the removed node. If the
     * queue is empty, it returns INT_MIN which symbols an empty queue.
     */
    T deq(int threadID, int operationNumber) {
        LogEntry* log = createDeqLog(threadID, operationNumber);
	while (true) {
            NodeWithLog* first = head.load();
            NodeWithLog* last = tail.load();
            NodeWithLog* next = first->next.load();
	    if (first == head.load()) {
	        if (first == last) {
	            if (next == nullptr) {
			logs[threadID * PADDING]->status = true;
                        BARRIER(&(logs[threadID * PADDING]->status));
                        return INT_MIN;
		    }
                    BARRIER_OPT(&last->next);
                    tail.compare_exchange_strong(last, next);
                } else {
	            LogEntry* valid = nullptr;
	            if (next->logDeq.compare_exchange_strong(valid, log)) {
		        BARRIER(&next->logDeq);
	                next->logDeq.load()->node = next;  // Connect
                        BARRIER_OPT(&next->logDeq.load()->node); // log to removed node
                        head.compare_exchange_strong(first, next); // Update head
		        return next->value;
		    } else {  // Finish the other thread's operation
		        if (head.load() == first){  // Important! Same context!
  		            // Update and flush the relevant node in the log
	     	            next->logDeq.load()->node = next;
                            BARRIER_OPT(&next->logDeq.load()->node);
                            head.compare_exchange_strong(first, next);
			}
		    }
		}
	    }
	}
    }
    //-------------------------------------------------------------------------
    
    /* Tries to finish all the detectable operations from before the last
     * crash.
     */
    void recover(LogEntry** detectableOps) {
        updateHead(head);  // Update head to point to the correct location
        // Update tail to point to the correct location and the status of
        // all the inserted nodes so that operations won't be executed twice
        updateTailAndStatus(head, tail);
        // Execute all unfinished operations from logs array
        finishPrevOperations(detectableOps);
        createNewArray(detectableOps);  // Create new array for current session
    }
    
    //-------------------------------------------------------------------------
    
    /* Create new array for current session. This array is created after all
     * operations from before the last crash are finished.
     */
    void createNewArray(LogEntry** detectableOps) {
        LogEntry** newLogs = new LogEntry*[MAX_THREADS * PADDING];
            for (int i = 0; i < MAX_THREADS; i++) {
                newLogs[i * PADDING] = nullptr;
                BARRIER_OPT(&newLogs[i * PADDING]);
            }
        CAS(&this->logs, detectableOps, newLogs);  // Update array
        BARRIER(&this->logs);
    }
    
    //-------------------------------------------------------------------------

    /* Update head to point to the last node that has a non-NULL logDeq
     * field. It also flushes and finishes the last visible remove operation.
     * (It might run in parallel to other dequeue operations as well.)
     */
    void updateHead(NodeWithLog* start) {
        NodeWithLog* temp = start->next.load();
        while (true) {
            if (!temp || !temp->logDeq.load()) {  // Head is updated
                return;
            }
            if (temp->next.load() && !temp->next.load()->logDeq.load()) {
                BARRIER(&temp->logDeq);
                temp->logDeq.load()->node = next; // Connect log to removed node
                BARRIER_OPT(&temp->logDeq->node);
                head.compare_exchange_strong(start, temp); // Update head
                return;
            }
            NodeWithLog* next = temp->next.load();
            temp = next;
        }
    }


    //-------------------------------------------------------------------------

    /* Traverse from head to one node before the last node that has a non-NULL
     * next field. During the traversal, update the status of all the logEnq
     * operations so they won't be executed twice. It also flushes and finishes
     * the last visible insert operation. (It might run in parallel to other
     * enqueue operations as well.)
     */
    void updateTailAndStatus(NodeWithLog* start, NodeWithLog* prevTail) {
        NodeWithLog* temp = start.load();
        temp->logEnq.load()->status = true;
        while (true) {
            if (!temp->next.load()) {
                tail.compare_exchange_strong(prevTail, temp); // Update tail
                return;
            }
            if (!temp->next.load()->next.load()) {
                BARRIER(&temp->next);
                temp->next.load()->logEnq.load()->status = true;
                tail.compare_exchange_strong(prevTail, temp->next.load());
                return;
            }
            NodeWithLog* next = temp->next.load();
            temp = next;
            temp->logEnq.load()->status = true;
        }
    }


    //-------------------------------------------------------------------------

    /* Traverse the logs array and finish all the detectable and unfinished
     * operations. Unfinished logDeq would miss the pointer of the removed
     * node and logEnq would miss a status that has a true value.
     */
    void finishPrevOperations(LogEntry** detectableOps) {
        for (int i = 0; i < MAX_THREADS; i++) {
            if (detectableOps[i * PADDING]) {
                Action action = detectableOps[i * PADDING]->action;
                if (action == insert) {
                    finishInsert(detectableOps[i * PADDING]);
                } else if (action == remove) {
                    finishRemove(detectableOps[i * PADDING]);
                }
            }
        }
    }


    //-------------------------------------------------------------------------

    /* Finishes an insert operation from the logs array. */
    void finishInsert(LogEntry* entry) {
        while (true) {
            NodeWithLog* last = tail.load();
            NodeWithLog* next = last->next.load();
            if (entry->status != true) {  // Recheck status
                if (last == tail.load()) {
                    if (next == nullptr) {
                        // Try to insert
                        if (last->next.compare_exchange_strong(nullptr, entry->node)) {
                            BARRIER(&last->next);
                            last->next.load()->logEnq.load()->status = true;
                            tail.compare_exchange_strong(last, entry->node);
                            return;
                        }
                    } else {  // If next is a node, finish the previous operation
                              // and help promote the tail
                        BARRIER(&last->next);
                        last->next.load()->logEnq.load()->status = true;
                        tail.compare_exchange_strong(last, next);
                    }
                }
            }
        }
    }


    //-------------------------------------------------------------------------

    /* Finishes a remove operation from the logs array. */
    void finishRemove(LogEntry* entry) {
        while (true) {
            NodeWithLog* first = head.load();
            NodeWithLog* last = tail.load();
            NodeWithLog* next = first->next.load();
            if (!entry->node || entry->status != true) {  // Recheck status
                if (first == head.load()) {
                    if (first == last) {
                        if (next == nullptr) {
                            entry->status = true;
                            BARRIER(&entry->status);
                            return;
                        }
                        BARRIER(&last->next);
                        last->next.load()->logEnq->status = true;
                        tail.compare_exchange_strong(last, next);
                    } else {
                        if (next->logDeq.compare_exchange_strong(nullptr,
                                                                 entry)) {
                            BARRIER(&next->logDeq);
                            next->logDeq->node = next;  // Connect log to removed
                            BARRIER_OPT(&next->logDeq->node);  // node
                            head.compare_exchange_strong(first, next);
                            return;
                        } else {  // Finish the other thread's operation
                            if (head.load() == first){  // Same context!
                                // Update and flush the relevant node in the log
                                next->logDeq.load()->node = next;
                                BARRIER_OPT(&next->logDeq->node);
                                head.compare_exchange_strong(first, next);
                            }
                        }
                    }
                }
            }
        }
    }
    
    //-------------------------------------------------------------------------


private:

    std::atomic<NodeWithLog*> head;
    int padding[PADDING];
    std::atomic<NodeWithLog*> tail;


    /* Creates a log object for the remove operation and connects it to the
     * array in the relevant entry according to the thread id. */
    LogEntry* createDeqLog(int threadID, int operationNumber) {
	LogEntry* log = new LogEntry(false, nullptr, remove, operationNumber);
	BARRIER(log);

	logs[threadID * PADDING] = log;  // Connect the log to its entry
	BARRIER(&logs[threadID * PADDING]);
	return log;
    }
    //-------------------------------------------------------------------------

    /* Creates a log object for the insert operation and connects it to the
     * array at the relevant entry according to the thread id. It connects
     * the log entry to the new node. */
    NodeWithLog* createEnqLogAndNode(T value, int threadID, int operationNumber) {
        LogEntry* log = new LogEntry(false, nullptr, insert, operationNumber);
	NodeWithLog* node = new NodeWithLog(value);

	log->node = node;  // Connect log to node
	node->logEnq = log;  // Connect node to log
        BARRIER_OPT(node);  // Flush node's and log's contents
	BARRIER(log);

	logs[threadID * PADDING] = log;  // Connect log to the thread's entry
	BARRIER(&logs[threadID * PADDING]);  // Flush the entry content

	return node;
    }
};


#endif /* LOG_QUEUE_H_ */

