#ifndef THREAT_H
#define THREAT_H

struct Threat
{
    int alertID;
    float missDistance;
    float relativeVelocity;
    float timeToTCA;
    float covarianceTrace;
};

#endif