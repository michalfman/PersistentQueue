#ifndef RELAXED_QUEUE_H_
#define RELAXED_QUEUE_H_

#include <atomic>
#include "Utilities.h"
#include <iostream>
#include <exception>
#include <vector>
#include <sstream>
using namespace std;


//=====================Start RelaxedQueue Class======================//
/* This queue preserves the buffered durable linearizability definition. This
 * version DOES NOT contain any memory management.It contains a
 * sync() function that takes a snapshot of the queue and makes all the nodes
 * between the previous tail and the current tail durable. This version is
 * also optimized for big queues. It contains the following fields:
 * head    - a pointer to the beginning of the queue. Points to a dummy node.
 * tail    - a pointer to the end of the queue.
 * data    - contains the head, tail and version of the last queue that was
 *           made durable by the sync() function.
 * counter - a global counter that is raised every time a thread
 *           tries to take a snapshot of the queue by calling to the sync()
 *           function.
 */
template <class T> class RelaxedQueue {
  public:

    //============================Start Node Class===========================//
    /* Node is the type of the elements that will be in the queue.
    * It contains the following fields:
    * value - can be of any type. It holds the data of the element.
    * next -  a pointer to the next element in the queue.
    */
    class Node {
      public:
	T value;
        std::atomic<Node*> next;
        Node(T val) : value(val), next(nullptr) {}
        Node() : value(T()), next(nullptr) {}
	virtual ~Node(){}
    };
    //============================End Node Class=============================//

    //=========================Start LastNVMData Class=======================//
    /* Holds the last version of the queue that was made durable. The queue
     * consists out of all the nodes between the head and the tail. It
     * contains the following fields:
     *  NVMTail - a pointer to the end of the durable queue.
     *  NVMHead - a pointer to the head of the durable queue.
     *  counter - the version of the last durable queue.
     */
    class LastNVMData {
      public:
        std::atomic<Node*> NVMTail;
        std::atomic<Node*> NVMHead;
        long counter;
    };
    //=========================End LastNVMData Class=========================//

    //==========================Start Invalid Class==========================//
    /* The purpose of this class is to make a temporal blocking for the tail.
     * This block is attached to the tail of the queue in order to take a valid
     * snapshot of the tail and the head. It inherits from the node class and
     * contains the following additional fields:
     * counter - symbols a potential version of the durable queue. It holds the
     *           version of the current thread that tries to take a snapshot of
     *           the queue.
     * tail -    a pointer to the end of the potential durable queue. This tail
     *           is the tail that the Invalid object is attached to.
     * head -    a pointer to the potential head of the durable queue. The
     *           thread would try to make all the nodes between the head and
     *           tail durable.
     */
    class Invalid: public Node {
      public:
        int counter;
        std::atomic<Node*> tail;
        std::atomic<Node*> head;
        Invalid(long c) : counter(c), tail(nullptr), head(nullptr) {}
        Invalid() : counter(0), tail(nullptr), head(nullptr) {}
        Invalid& operator=(const Invalid& i) {
            counter = i.counter;
            tail = i.tail.load();
            head = i.head.load();
            return *this;
        }
    };
    //==========================End Invalid Class============================//

    /* The constructor of the queue. Makes the head and tail point to a durable
     * dummy node. Updating the initial data(snapshot) to point to that node as
     * well.
     */
    RelaxedQueue() {
        Node* dummy = new Node(INT_MAX);
	BARRIER(dummy);  // Flush the dummy node before connecting it
	head = tail = dummy;
	BARRIER(&head);
	BARRIER(&tail);
	LastNVMData* d = new LastNVMData();
	d->NVMTail = dummy;
	d->NVMHead = dummy;
	d->counter = -1;
	BARRIER(d);
	data = d;
	BARRIER(&data);
	counter = ATOMIC_VAR_INIT(0);
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
        Node* node = new Node(value);
	while (true) {
            Node* last = tail.load();
            Node* next = last->next.load();
	    if (last == tail.load()) {
		if (next == nullptr) {
                    if (last->next.compare_exchange_strong(next, node)) {
                        tail.compare_exchange_strong(last, node);
			return;
		    }
		} else {
		    Node* n = (Node*)next;
		    Invalid* currI = dynamic_cast<Invalid*>(n);
		    if (currI != nullptr) {  // Check if next is the Invalid node
                        // Help finish taking a snapshot
			Node* valid = nullptr;
                        currI->head.compare_exchange_strong(valid, head);
                        // Remove block
                        currI->tail.load()->next.compare_exchange_strong(n, nullptr);
			continue;
		    }
                    // If next is a regular node, help in promoting the tail
                    tail.compare_exchange_strong(last, next);
		}
	    }
	}
    }
    //-------------------------------------------------------------------------

    /* Tries to dequeue a node. Returns the value of the removed node. If the
     * queue is empty, it returns INT_MIN which symbols an empty queue.
     */
    T deq(){
        while (true) {
            Node* first = head.load();
            Node* last = tail.load();
            Node* next = first->next.load();
	    if (first == head.load()) {
	        if (first == last) {
		    if (next == nullptr) {   // The queue is empty
			return INT_MIN;
		    }
		    Node* n = (Node*)next;
		    Invalid* currI = dynamic_cast<Invalid*>(n);
		    if (currI != nullptr) {  // Check if next is the Invalid node
                        // Help finish taking the snapshot
                        Node* valid = nullptr;
			currI->head.compare_exchange_strong(valid, head);
                        // Remove block
                        currI->tail.load()->next.compare_exchange_strong(n, nullptr);
			return INT_MIN;
		    }
                    // If next is a regular node, help promote the tail
                    tail.compare_exchange_strong(last, next);
		} else {
		    T value = next->value;
                    if (head.compare_exchange_strong(first, next)) {
                        return value;
		    }
		}
	    }
	}
    }
    //-------------------------------------------------------------------------

    /* Blocks the tail and takes valid snapshot of the queue. The snapshot
     * will be contained out of the tail that was blocked and a head that
     * was sampled afterwards. The parameters of the function:
     * invalid - the object that we are blocking the tail with.
     */
    bool blockTheTail(Invalid* invalid) {
        LastNVMData* currData = data.load();
	int currentCounter = atomic_fetch_add(&counter, 1);
	Invalid* currI = nullptr;
	while (true) {
            // Checks whether the snapshot was taken by a more progressed thread
	    if (currData->counter > currentCounter) {
	        return false;
	    }
	    invalid->counter = currentCounter;
            Node* last = tail.load();
            Node* next = last->next.load();
	    if (last == tail.load()) {
	        if (next == nullptr) {
	            invalid->tail = last;
                    // Block the tail
                    if (last->next.compare_exchange_strong(next, invalid)) {
                        // Update head
			Node* valid = nullptr;
                        invalid->head.compare_exchange_strong(valid, head);
                        // Remove block
			Node* invalidNode = (Node*)invalid;
                        last->next.compare_exchange_strong(invalidNode, nullptr);
                        return true;
                    }
	        } else {
	 	    Node* n = (Node*)next;
	            currI = dynamic_cast<Invalid*>(n);
	            if (currI != nullptr) {  // Another thread is syncing
	                Node* valid = nullptr;
			if (currI->counter > currentCounter ||
	                    currI->head == nullptr) {  // Sync the same range
			    currI->head.compare_exchange_strong(valid, head);
                            currI->tail.load()->next.compare_exchange_strong(n, nullptr);
	                    *invalid = *currI;
	                    return true;
	                }
                        // Help finish
                        currI->head.compare_exchange_strong(valid, head);
                        currI->tail.load()->next.compare_exchange_strong(n, nullptr);
	                    continue;  // Try again cause the other sync is old
	            }
                    tail.compare_exchange_strong(last, next);
	        }
	    }
	}
    }

    //-------------------------------------------------------------------------
    /* Makes all the nodes between start and end durable.
     * The parameters of the function:
     * start         - the node we start making all node durable from.
     * end           - the last node we make durable.
     */
    void makeDurble(Node* start, Node* end) {
        Node* temp = start;
        BARRIER(temp);
        while(1) {
            if (temp == end) {
                return;
            }
            Node* next = temp->next.load();
            BARRIER(next);
            temp = next;
        }
    }
    //-------------------------------------------------------------------------

    /* Takes a valid snapshot of the queue. If another thread with a bigger
     * snapshot version runs concurrently - helps finish the operation if
     * necessary and returns. Otherwise, does the following two steps:
     * 1. Blocks the tail and takes a valid snapshot.
     * 2. Makes all the nodes in the snapshot durable.
     */
    void sync(int threadID) {
	int currentCounter = 0;
	Invalid* invalid = new Invalid(currentCounter);
	while (true) {
	    // Block the tail and take a snapshot.
            LastNVMData* currData = data.load();
	    bool result = blockTheTail(invalid);
	    if (result == false) { // Another took more updated snapshot
	        return;
	    }

            // Flush all the nodes between the last and the current tail
	    makeDurble(currData->NVMTail.load(), invalid->tail.load());

	    // Try to update snapshot
	    LastNVMData* potential = new LastNVMData();
	    potential->NVMTail = invalid->tail.load();
    	    potential->NVMHead = invalid->head.load();
	    potential->counter = invalid->counter;
	    BARRIER(potential);
            // currData->counter is smaller than invalid->counter because sampeled
            // before blocking the tail
	    if (data.compare_exchange_strong(currData, potential)) {
		BARRIER(data);
		break;
	    } else {
		continue;
	    }
	}
	return;
    }
    //-------------------------------------------------------------------------

    /* This is another way of implementing the sync. If the queue is very small,
     * this might be a better way once the flushes will not invalidate the cache
     * when they are called. This sync fulshes everything between the head and
     * the tail instead of flushing everyhitng between the previos and the
     * current tail.
     */ 
     /*void sync(int threadID) {
	int currentCounter = 0;
	Invalid* invalid = new Invalid(currentCounter);
	while (true) {
	    // Block the tail and take a snapshot
	    bool result = blockTheTail(invalid);
	    if (result == false) { // Another took a more updated snapshot
		return;
	    }

	    // Flush all the nodes between the head and the tail
	    makeDurble(invalid->head.load(), invalid->tail.load());

 	    // Try to update the snapshot
            LastNVMData* currData = data.load();
	    LastNVMData* potential = new LastNVMData();
	    potential->NVMTail = invalid->tail.load();
	    potential->NVMHead = invalid->head.load();
	    potential->counter = invalid->counter;
	    while (true) {
                // Check if the potential snapshot is the most updated
		if (currData->counter < invalid->counter) {
		    BARRIER(potential);
                    if (data.compare_exchange_strong(currData, potential)) {
			BARRIER(data);
	    		break;
	       	    } else {  // Another thread has managed to update
	    		currData = data.load();
	       	    }
		} else {  // The potential snapshot has a lower version than
			  // the snapshot that was updated. Can finish.
		    break;
		}
	    }
	    return;
	}
    }*/
  
  private:

    std::atomic<Node*> head;
    int padding1[PADDING];
    std::atomic<Node*> tail;
    int padding2[PADDING];
    std::atomic<LastNVMData*> data;
    int padding3[PADDING];
    atomic<int> counter;

};

//======================End RelaxedQueue Class=======================//

#endif /* RELAXED_QUEUE_H_ */

