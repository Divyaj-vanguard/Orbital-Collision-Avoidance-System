#include <stdio.h>
#include "threat.h"
void displayThreat(Threat *t)
{
printf("\n--- Orbital Threat----\n");
printf("Alert ID         : %d\n", t->alertID);
printf("Miss Distance    : %.2f m\n", t->missDistance);
printf("Relative Velocity: %.2f km/s\n", t->relativeVelocity);
printf("Time to TCA      : %.2f sec\n", t->timeToTCA);
printf("Covariance Trace : %.2f m^2\n", t->covarianceTrace);
}