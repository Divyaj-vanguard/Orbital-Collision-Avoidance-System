#include <stdio.h>
#include "queue.h"

void initQueue(Queue *q)
{
q->front = -1;
q->rear = -1;
}

int isEmpty(Queue *q)
{
return q->front == -1;
}

int isFull(Queue *q)
{
return q->rear == MAX - 1;
}

void enqueue(Queue *q, Threat t)
{
if(isFull(q))
{
printf("Queue Overflow\n");
 return;
}

if (q->front == -1)
{
q->front = 0;
}
q->rear++;
q->arr[q->rear] = t;
}

Threat dequeue(Queue *q)
{
Threat empty = {-1, 0, 0, 0, 0};

if (isEmpty(q))
{
printf("Queue Underflow\n");
return empty;
}

Threat t = q->arr[q->front];

if (q->front == q->rear)
{
q->front = -1;
q->rear = -1;
}
else
{
q->front++;
}

return t;
}

Threat peek(Queue *q)
{
Threat empty = {-1, 0, 0, 0, 0};
if(isEmpty(q))
{
printf("Queue is Empty\n");
return empty;
}
return q->arr[q->front];
}

void displayQueue(Queue *q)
{
if(isEmpty(q))
{
printf("Queue is Empty\n");
return;
}

printf("\n--- Pending Threats ---\n");

for (int i = q->front; i <= q->rear; i++)
{
        printf("\nAlert ID: %d\n",
               q->arr[i].alertID);

        printf("Miss Distance: %.2f m\n",
               q->arr[i].missDistance);

        printf("Relative Velocity: %.2f km/s\n",
               q->arr[i].relativeVelocity);

        printf("Time to TCA: %.2f sec\n",
               q->arr[i].timeToTCA);

        printf("Covariance Trace: %.2f m^2\n",
               q->arr[i].covarianceTrace);

        printf("-----------------------\n");
}
}