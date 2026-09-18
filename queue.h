#ifndef QUEUE_H
#define QUEUE_H
#include "../Structure/threat.h"
#define MAX 100

typedef struct
{
    Threat arr[MAX];
    int front;
    int rear;
} Queue;

void initQueue(Queue *q);

int isEmpty(Queue *q);
int isFull(Queue *q);

void enqueue(Queue *q, Threat t);
Threat dequeue(Queue *q);
Threat peek(Queue *q);
void displayQueue(Queue *q);

#endif