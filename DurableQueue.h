
#ifndef DURABLE_QUEUE_H_
#define DURABLE_QUEUE_H_

#include <atomic>
#include "Utilities.h"

//===========================Start DurableQueue Class==========================//
/* This queue preserves the durable linearizability definitions. This version
 * does NOT contain any memory management. Every returned value from a dequeue
 * operation is saved within the returned values array in case there is a crash
 * after ther dequeue and before the value was returned to the caller. However,
 * this array is not necessaty for satisfying durable inearizability.
 */
template <class T> class DurableQueue {

  public:

    //====================Start NodeWithID Class==========================//
    /* NodeWithID is the type of the elements that will be in the queue.
     * It contains the following fields:
     * value     - can be of any type. It holds the data of the element.
     * next      - a pointer to the next element in the queue.
     * threadID  - holds the id of the thread that manages to dequeue this
     *             node. Helps for saving the returned value before a crash.
     */
    class NodeWithID {
      public:
        T value;
        std::atomic<NodeWithID*> next;
        std::atomic<int> threadID;
        NodeWithID(T val) : value(val), next(nullptr), threadID(-1) {}
        NodeWithID() : value(T()), next(nullptr), threadID(-1) {}
    };
    //====================End NodeWithID Class==========================//

    // The removedValues array. Each thread has an entrance where is saves
    // the value of the last node it managed to dequeue. Relevant in case
    // there is a crash after the value was removed and before the value
    // was returned to the caller.
    T* removedValues[MAX_THREADS * PADDING];

    DurableQueue() {
        head = tail = new NodeWithID(INT_MAX);
        BARRIER(tail);
        BARRIER(&tail);
        BARRIER(&head);
        for (int i = 0; i < MAX_THREADS; i++) {
            removedValues[i * PADDING] = nullptr;
            BARRIER(&removedValues[i * PADDING]);
        }
    }

    //-------------------------------------------------------------------------
	
    void initialize() {
        for (int i = 0; i < QUEUE_SIZE; i++){
            enq(i+1);
        }
    }
    //-------------------------------------------------------------------------
    
    /* Enqueues a node to the queue with the given value. */
    void enq(T value) {
        NodeWithID* node = new NodeWithID(value);
        BARRIER(node);
        while (true) {
            NodeWithID* last = tail.load();
            NodeWithID* next = last->next.load();
            if (last == tail.load()) {
                if (next == nullptr) {
                    if (last->next.compare_exchange_strong(next, node)) {
                        BARRIER_OPT(&last->next);
                        tail.compare_exchange_strong(last, node);
                        return;
                    }
                } else {
                    BARRIER_OPT(&last->next);
                    tail.compare_exchange_strong(last, next);
                }
            }
        }
    }


    //-------------------------------------------------------------------------
    
    /* Tries to dequeue a node. Returns the value of the removed node. If the
     * queue is empty, it returns INT_MIN which symbols an empty queue. In
     * addition, it saves the returned value in the thread's location at the
     * returnedValues arreay. In order to remove the value, it first stamps the
     * value with the threadID - this is what indicates that the node was
     * removed.
     */
    T deq(int threadID) {
        T* newRemovedValue = new T(INT_MAX);
        BARRIER(newRemovedValue);
        removedValues[threadID * PADDING] = newRemovedValue;
        BARRIER(&removedValues[threadID * PADDING]);
        while (true) {
            NodeWithID* first = head.load();
            NodeWithID* last = tail.load();
            NodeWithID* next = first->next.load();
            if (first == head.load()) {
                if (first == last) {
                    if (next == nullptr) {
                        *removedValues[threadID * PADDING] = INT_MIN;
                        BARRIER(removedValues[threadID * PADDING]);
                        return INT_MIN;
                    }
                    BARRIER_OPT(&last->next);
                    tail.compare_exchange_strong(last, next);
                } else {
                    T value = next->value;
                    // Mark the node as removed by changing the threadID field
		    int valid = -1;
                    if (next->threadID.compare_exchange_strong(valid, threadID)) {
                        BARRIER(&next->threadID);
                        *removedValues[threadID * PADDING] = value;
                        BARRIER_OPT(removedValues[threadID * PADDING]);
                        head.compare_exchange_strong(first, next); // Update head
                        return value;
                    } else {
                        T* address = removedValues[next->threadID * PADDING];
                        if (head.load() == first){ //same context
                            BARRIER(&next->threadID);
                            *address = value;
                            BARRIER_OPT(address);
                            head.compare_exchange_strong(first, next);
                        }
                    }
                }
            }
        }
    }

    
    //-------------------------------------------------------------------------


  private:
    std::atomic<NodeWithID*> head;
    int padding[PADDING];
    std::atomic<NodeWithID*> tail;

};

#endif /* DURABLE_QUEUE_H_ */

































