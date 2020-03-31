#include <stdio.h>
#include "SpreadSimulator.h"

int main()
{
    printf("Hello, World!\n");

    SpreadSimulationSettings* settings = createSimSettings(
        900,
        5,
        850,
        200,
        50,
        450,
        20,
        50,
        50,
        5,
        600,
        600);

    SpreadSimulator* simulator = createSpreadSimulator(settings);

    for (int i = 0; i < 120; i++)
    {
        printf("Unaffected: %i, Infected: %i, Cured: %i, Dead: %i \n", simulator->unaffected, simulator->infected, simulator->cured, simulator->dead);
        tick(simulator);
    }

    freeSpreadSimulator(simulator);

    return(0);
}
