#include <iostream>
#include <queue>
#include <thread>
#include <mutex>
#include <cstring>
#include <pybind11/embed.h>

#include "Threat.h"
#include "CircularQueue.h"
using namespace std;
namespace py = pybind11;


// Comparator for Priority Queue
struct ThreatComparator
{
    bool operator()(const Threat& t1, const Threat& t2) const
    {
        return t1.priority < t2.priority;
    }
};


// Function to call Python ML model
string predictRisk(Threat t)
{
    py::module_ pythonModule = py::module_::import("predict");

    py::object result = pythonModule.attr("predict_risk")(
        t.missDistance,
        t.relativeVelocity,
        t.timeToTCA,
        t.covarianceTrace
    );

    return result.cast<string>();
}


// Convert Risk Level into Priority
int getPriority(string riskLevel)
{
    if (riskLevel == "Critical")
        return 4;
    else if (riskLevel == "High")
        return 3;
    else if (riskLevel == "Caution")
        return 2;
    else
        return 1;
}


// Display threats currently waiting in Normal Queue
void displayQueue(queue<Threat> q)
{
    if (q.empty())
    {
        cout << "\nNo pending threats.\n";
        return;
    }

    cout << "\n-------------------------------------\n";
    cout << "          PENDING THREATS\n";
    cout << "-------------------------------------\n";

    while (!q.empty())
    {
        Threat t = q.front();

        cout << "\nAlert ID          : " << t.alertID << endl;
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
    // Set Python installation path
    Py_SetPythonHome(
        L"C:\\Users\\HP\\AppData\\Local\\Programs\\Python\\Python313"
    );

    // Start embedded Python interpreter
    py::scoped_interpreter guard{};

    // Add required Python paths
    py::module_ sys = py::module_::import("sys");

    sys.attr("path").attr("insert")(
        0,
        "C:\\Users\\HP\\Desktop\\AutonomousOrbitalCollisionSystem\\ml"
    );

    sys.attr("path").attr("insert")(
        0,
        "C:\\Users\\HP\\AppData\\Local\\Programs\\Python\\Python313\\Lib\\site-packages"
    );


    // Normal Queue
    queue<Threat> threatQueue;

    // Internal Circular Queue
    CircularQueue circularQueue;

    // Priority Queue
    priority_queue<Threat, vector<Threat>, ThreatComparator> priorityQueue;


    // Menu choice
    int choice;


    do
    {
        cout << "\n------------------------------------\n";
        cout << "       ORBITAL THREAT SYSTEM\n";
        cout << "------------------------------------\n";
        cout << "1. Add Threat\n";
        cout << "2. View Pending Threats\n";
        cout << "3. Send Threat to Process\n";
        cout << "4. View Highest Priority Threat\n";
        cout << "5. Exit\n";

        cout << "------------------------------------\n";

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
                    // Get threat from Normal Queue
                    Threat t = threatQueue.front();
                    threatQueue.pop();

                    // Send threat to Circular Queue
                    circularQueue.enqueue(t);

                    cout << "\nThreat sent for processing successfully.\n";
                    cout << "Alert ID: " << t.alertID << endl;


                    // Remove threat from Circular Queue for ML processing
                    Threat processedThreat = circularQueue.dequeue();


                    // Predict risk using ML model
                    processedThreat.riskLevel =
                        predictRisk(processedThreat);


                    // Convert risk level into priority
                    processedThreat.priority =
                        getPriority(processedThreat.riskLevel);


                    // Store processed threat in Priority Queue
                    priorityQueue.push(processedThreat);


                    // Display ML result
                    cout << "\n========== ML RESULT ==========\n";

                    cout << "Alert ID   : "
                         << processedThreat.alertID << endl;

                    cout << "Risk Level : "
                         << processedThreat.riskLevel << endl;

                    cout << "Priority   : "
                         << processedThreat.priority << endl;
                }

                break;
            }


            // EXIT
            case 5:
            {
                cout << "\nExiting system...\n";

                break;
            }

            case 4:
{
    if (priorityQueue.empty())
    {
        cout << "\nNo processed threats available.\n";
    }
    else
    {
        Threat t = priorityQueue.top();

        cout << "\n====================================\n";
        cout << "       HIGHEST PRIORITY THREAT\n";
        cout << "====================================\n";

        cout << "Alert ID          : " << t.alertID << endl;
        cout << "Miss Distance     : " << t.missDistance << " m" << endl;
        cout << "Relative Velocity : " << t.relativeVelocity << " km/s" << endl;
        cout << "Time to TCA       : " << t.timeToTCA << " sec" << endl;
        cout << "Covariance Trace  : " << t.covarianceTrace << " m^2" << endl;
        cout << "Risk Level        : " << t.riskLevel << endl;
        cout << "Priority          : " << t.priority << endl;
    }

    break;
}


            default:
            {
                cout << "\nInvalid choice.\n";
            }
        }

    } while (choice != 5);


    return 0;
}