#include <iostream>
#include "CircularQueue.h"
using namespace std;

CircularQueue::CircularQueue()
{
front = 0;
rear = -1;
size = 0;}

bool CircularQueue::isEmpty()
{
return size == 0;}

bool CircularQueue::isFull()
{
return size == 5;}

void CircularQueue::enqueue(Threat t)
{
if (isFull())
{
cout << "Circular Queue is Full." << endl;
return;
}
rear = (rear + 1) % 5;
arr[rear] = t;
size++;
}

Threat CircularQueue::dequeue()
{
if (isEmpty())
{
cout << "Circular Queue is Empty." << endl;
return Threat{};
}
Threat t = arr[front];
front = (front + 1) % 5;
size--;
return t;
}

Threat CircularQueue::peek()
{
if (isEmpty())
{
cout << "Circular Queue is Empty." << endl;
return Threat{};
}
return arr[front];
}

void CircularQueue::display()
{
if (isEmpty())
{
cout << "Circular Queue is Empty." << endl;
return;
}
cout << "\n========== CIRCULAR QUEUE ==========\n";
int index = front;
for (int i = 0; i < size; i++)
{
Threat t = arr[index];
cout << "\nAlert ID        : " << t.alertID << endl;
cout << "Miss Distance     : " << t.missDistance << " m" << endl;
cout << "Relative Velocity : " << t.relativeVelocity << " km/s" << endl;
cout << "Time to TCA       : " << t.timeToTCA << " sec" << endl;
cout << "Covariance Trace  : " << t.covarianceTrace << " m^2" << endl;
index = (index + 1) % 5;
}
}