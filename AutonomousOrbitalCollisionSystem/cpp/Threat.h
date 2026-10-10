#ifndef THREAT_H
#define THREAT_H
#include<iostream>
#include<string>
using namespace std;

struct Threat
{
    int alertID;
    float missDistance;
    float relativeVelocity;
    float timeToTCA;
    float covarianceTrace;

    string riskLevel;
    int priority;
};

#endif