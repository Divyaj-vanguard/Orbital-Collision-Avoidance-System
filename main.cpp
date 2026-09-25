#include <iostream>
#include <queue>
#include "Threat.h"
#include "CircularQueue.h"

using namespace std;


void displayQueue(queue<Threat> q)
{
    if (q.empty())
    {
        cout << "\nNo pending threats.\n";
        return;
    }

    cout << "\n-------------------------------------\n";
    cout << "          PENDING THREATS\n";
    cout << "-----------------------------------------\n";

    while (!q.empty())
    {
        Threat t = q.front();

        cout << "\nAlert ID        : " << t.alertID << endl;
        cout << "Miss Distance     : " << t.missDistance << " m" << endl;
        cout << "Relative Velocity : " << t.relativeVelocity << " km/s" << endl;
        cout << "Time to TCA       : " << t.timeToTCA << " sec" << endl;
        cout << "Covariance Trace  : " << t.covarianceTrace << " m^2" << endl;

        cout << "-----------------------------\n";

        q.pop();
    }
}

int main()
{

    queue<Threat> threatQueue;
    CircularQueue circularQueue;

    int choice;

    do
    {
        cout << "\n------------------------------------\n";
        cout << "       ORBITAL THREAT SYSTEM\n";
        cout << "----------------------------------------\n";
        cout << "1. Add Threat\n";
        cout << "2. View Pending Threats\n";
        cout << "3. Send Threat to Process\n";
        cout << "4. Exit\n";
        cout << "--------------------------------------\n";

        cout << "Enter your choice: ";
        cin >> choice;

        switch (choice)
        {

            // ADD THREAT

            case 1:
            {
                Threat t;

                cout << "\nEnter Alert ID: ";
                cin >> t.alertID;

                cout << "Enter Miss Distance (m): ";
                cin >> t.missDistance;

                cout << "Enter Relative Velocity (km/s): ";
                cin >> t.relativeVelocity;

                cout << "Enter Time to TCA (sec): ";
                cin >> t.timeToTCA;

                cout << "Enter Covariance Trace (m^2): ";
                cin >> t.covarianceTrace;

                threatQueue.push(t);

                cout << "\nThreat added successfully.\n";

                break;
            }

            // VIEW PENDING THREATS

            case 2:
            {
                displayQueue(threatQueue);

                break;
            }

            // SEND THREAT TO PROCESS

            case 3:
            {
                if (threatQueue.empty())
                {
                    cout << "\nNo pending threats to process.\n";
                }
                else if (circularQueue.isFull())
                {
                    cout << "\nProcessing system is currently full.\n";
                }
                else
                {
                    // Get the first threat from Normal Queue
                    Threat t = threatQueue.front();

                    // Remove it from Normal Queue
                    threatQueue.pop();

                    // Send it to internal Circular Queue
                    circularQueue.enqueue(t);

                    cout << "\nThreat sent for processing successfully.\n";
                    cout << "Alert ID: " << t.alertID << endl;
                }

                break;
            }

            // EXIT
            case 4:
            {
                cout << "\nExiting system...\n";
                break;
            }

            default:
            {
                cout << "\nInvalid choice.\n";
            }
        }

    } while (choice != 4);

    return 0;
}