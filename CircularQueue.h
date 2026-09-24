#ifndef CIRCULAR_QUEUE_H
#define CIRCULAR_QUEUE_H
#include "Threat.h"

class CircularQueue
{
private:
 Threat arr[5];
 int front;
 int rear;
 int size;

public:
 CircularQueue();

 bool isEmpty();
 bool isFull();

 void enqueue(Threat t);
 Threat dequeue();
 Threat peek();

 void display();
};
#endif