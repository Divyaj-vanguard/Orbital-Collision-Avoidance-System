#ifndef THREAT_H
#define THREAT_H

typedef struct
{
    int alertID;
    float missDistance;
    float relativeVelocity;
    float timeToTCA;
    float covarianceTrace;
} Threat;
void displayThreat(Threat *t);
#endif