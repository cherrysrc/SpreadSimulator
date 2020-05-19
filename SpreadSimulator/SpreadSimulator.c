#include "SpreadSimulator.h"

SpreadSimulator createSimulator(SimulationSettings* settings)
{
    SpreadSimulator simulator;

    srand(time(NULL));

    simulator.settings = *settings;

    //Create initial statistics for simulation
    simulator.stats = createStats(settings->entityCount - settings->initialInfected, settings->initialInfected);

    //Allocate memory
    simulator.positions = (Vector2*) calloc(settings->entityCount, sizeof(Vector2));
    simulator.velocities = (Vector2*) calloc(settings->entityCount, sizeof(Vector2));
    simulator.accelerations = (Vector2*) calloc(settings->entityCount, sizeof(Vector2));
    simulator.medComponents = (MedicalComponent*) calloc(settings->entityCount, sizeof(MedicalComponent));

    simulator.centralLocations = (Vector2*) calloc(settings->centralLocationsCount, sizeof(Vector2));

    simulator.attractionScalar = 1;

    for (int i = 0; i < settings->entityCount; i++)
    {
        //Generate random position in confined space
        double xPos = rand() % settings->dimX;
        double yPos = rand() % settings->dimY;

        simulator.positions[i] = createVector(xPos, yPos);

        //Initialize Velocity using random vector
        //-2 to +2 for x,y
        double xVel = rand() % 4 - 2;
        double yVel = rand() % 4 - 2;

        simulator.velocities[i] = createVector(xVel, yVel);

        //Initial acceleration is zero
        simulator.accelerations[i] = createVector(0, 0);

        //Determine whether this entity is infected and/or mobile
        bool mobile = (i < simulator.settings.mobileCount);
        bool infected = (i < simulator.settings.initialInfected);

        simulator.medComponents[i] = createMedicalComponent(infected, mobile, settings->infectionChance,
                                                            settings->survivalChance, settings->detectionChance,
                                                            settings->influenceRadius);
    }

    for (int i = 0; i < settings->centralLocationsCount; i++)
    {
        double xPos = rand() % settings->dimX;
        double yPos = rand() % settings->dimY;
        simulator.centralLocations[i] = createVector(xPos, yPos);
    }

    return simulator;
}

void tick(SpreadSimulator* simulator)
{
    tickEvent(simulator);

    //Build a new Quadtree every frame
    Vector2 tmp_vec_1 = createVector(simulator->settings.dimX / 2, simulator->settings.dimY / 2);
    Vector2 tmp_vec_2 = createVector(simulator->settings.dimX / 2, simulator->settings.dimY / 2);
    Rect tmp_rect = createRect(&tmp_vec_1, &tmp_vec_2);
    Quadtree* tree = createQuadTree(&tmp_rect);

    //Insert all entity positions with their corresponding index
    for (int i = 0; i < simulator->settings.entityCount; i++)
    {
        PositionIndexPair pair = (PositionIndexPair) {i, (Vector2) {simulator->positions[i].x,
                                                                    simulator->positions[i].y}};
        bool success = insert(tree, pair);
    }

    //'Closing and opening' central locations
    if (simulator->stats.tick % 240 == 0)
    {
        simulator->attractionScalar *= -1;
    }

    //Central locations attraction calculation
    for (int i = 0; i < simulator->settings.centralLocationsCount; i++)
    {
        tmp_vec_1 = createVector(simulator->centralLocations[i].x, simulator->centralLocations[i].y);
        tmp_vec_2 = createVector(100, 100);
        Rect range = createRect(&tmp_vec_1, &tmp_vec_2);

        PositionIndexPairList query = queryRange(tree, &range);

        for (int qIndex = 0; qIndex < query.count; qIndex++)
        {
            int entityIndex = query.data[qIndex].index;

            Vector2 attraction = createVector(simulator->centralLocations[i].x - simulator->positions[entityIndex].x,
                                              simulator->centralLocations[i].y - simulator->positions[entityIndex].y);


            scaleVector(&attraction, simulator->attractionScalar);
            scaleVector(&attraction, 0.05); //arbitrary value, no specific choice
            addVector(&simulator->accelerations[entityIndex], &attraction);
        }

        freeList(query);
    }

    //Loop over every entity
    for (int i = 0; i < simulator->settings.entityCount; i++)
    {
        //Update pos, vel and acc for all entities, in case they're mobile
        if (simulator->medComponents[i].isMobile)
        {
            addVector(&simulator->positions[i], &simulator->velocities[i]);
            addVector(&simulator->velocities[i], &simulator->accelerations[i]);
            simulator->accelerations[i].x = 0;
            simulator->accelerations[i].y = 0;

            //bounce off the borders
            if (simulator->positions[i].x <= 0 || simulator->positions[i].x >= simulator->settings.dimX)
                simulator->velocities[i].x *= -1;

            if (simulator->positions[i].y <= 0 || simulator->positions[i].y >= simulator->settings.dimY)
                simulator->velocities[i].y *= -1;

            //Add repulsive force as distancing measure
            if (simulator->settings.activeDistancing)
            {
                //Query tree for entities in range
                //Calculate repulsion from them
                tmp_vec_1 = createVector(simulator->positions[i].x, simulator->positions[i].y);
                tmp_vec_2 = createVector(simulator->medComponents[i].infectionRadius * 2,
                                         simulator->medComponents[i].infectionRadius * 2);
                Rect range = createRect(&tmp_vec_1, &tmp_vec_2);

                PositionIndexPairList query = queryRange(tree, &range);

                for (int qIndex = 0; qIndex < query.count; qIndex++)
                {
                    int j = query.data[qIndex].index;

                    if (i == j) continue;

                    //No need to insert if they're already dead
                    if (simulator->medComponents[j].isDead) continue;

                    Vector2 repulsion = createVector(
                            simulator->positions[i].x - simulator->positions[j].x,
                            simulator->positions[i].y - simulator->positions[j].y
                    );

                    scaleVector(&repulsion, 0.025); //arbitrary value, no specific choice
                    addVector(&simulator->accelerations[i], &repulsion);
                }

                //free query
                freeList(query);
            }
        }

        //-----------------------------------------------------------
        //In case an entity is infected:
        //Find others in range and eventually infect them
        if (simulator->medComponents[i].isInfected)
        {
            //Query tree for entities in range
            //Calculate infection from them
            tmp_vec_1 = createVector(simulator->positions[i].x, simulator->positions[i].y);
            tmp_vec_2 = createVector(simulator->medComponents[i].infectionRadius,
                                     simulator->medComponents[i].infectionRadius);
            Rect range = createRect(&tmp_vec_1, &tmp_vec_2);

            PositionIndexPairList query = queryRange(tree, &range);

            for (int qIndex = 0; qIndex < query.count; qIndex++)
            {
                int j = query.data[qIndex].index;

                //Skip to prevent comparing an entity to itself
                if (i == j) continue;

                //Skip in case the other is already infected, dead or cured
                if (simulator->medComponents[j].isInfected || simulator->medComponents[j].isDead ||
                    simulator->medComponents[j].isCured)
                    continue;

                int rng = rand() % 100 + 1;
                if (rng <= simulator->medComponents[i].infectionChance)
                {
                    infectionEvent(simulator, j);
                }
            }

            //free query
            freeList(query);

            //Update tick counter on infected
            simulator->medComponents[i].ticksSinceInfection++;
            //Check if they're 'due'
            if (simulator->medComponents[i].ticksSinceInfection >= simulator->settings.ticksUntilExpiration)
            {
                int rng = rand() % 100 + 1;
                if (rng <= simulator->medComponents[i].survivalChance) //Patient survives
                {
                    cureEvent(simulator, i);
                } else //Patient dies
                {
                    deathEvent(simulator, i);
                }

                //In case he's been hospitalized
                if (simulator->medComponents[i].isInHospital)
                {
                    dehospitalizeEvent(simulator, i);
                }
            }
        }
        //Prevent entities gaining too much speed
        limitVector(&simulator->velocities[i], 1);
    }

    freeQuadtree(tree);

    //Perform tests on random entities
    for (int i = 0; i < simulator->settings.testsPerTick; i++)
    {
        if (simulator->stats.hospitalized >= simulator->settings.hospitalCapacity)
        {
            break;
        }

        //Pick a random entity
        int rngIndex = rand() % simulator->settings.entityCount;

        //In case the randomly selected entity is infected,
        //Detect him using a certain chance
        //Used to model test accuracy/quality
        if (simulator->medComponents[rngIndex].isInfected)
        {
            int probability = rand() % 100 + 1;
            //Additionally check if hes not in hospital already
            if (probability <= simulator->settings.detectionChance &&
                !simulator->medComponents[rngIndex].isInHospital)
            {
                hospitalizeEvent(simulator, rngIndex);
            }
        }
    }
}

void printStats(SpreadSimulator* simulator)
{
    printf("Tick: %i, Susceptible: %i, Infected: %i, Cured: %i, Dead: %i, Hospitalized: %i\n",
           simulator->stats.tick,
           simulator->stats.susceptible,
           simulator->stats.infected, simulator->stats.cured, simulator->stats.dead,
           simulator->stats.hospitalized);
}

void printStatsRaw(SpreadSimulator* simulator)
{
    printf("%i %i %i %i %i %i\n",
           simulator->stats.tick,
           simulator->stats.susceptible,
           simulator->stats.infected, simulator->stats.cured, simulator->stats.dead,
           simulator->stats.hospitalized);
}

//Functions for 'events'
//Makes tick method less messy and more readable
void tickEvent(SpreadSimulator* simulator)
{
    simulator->stats.tick++;
}

void infectionEvent(SpreadSimulator* simulator, int index)
{
    simulator->stats.susceptible--;
    simulator->stats.infected++;

    simulator->medComponents[index].isInfected = true;
}

void cureEvent(SpreadSimulator* simulator, int index)
{
    simulator->stats.infected--;
    simulator->stats.cured++;

    simulator->medComponents[index].isInfected = false;
    simulator->medComponents[index].isCured = true;
}

void deathEvent(SpreadSimulator* simulator, int index)
{
    simulator->stats.infected--;
    simulator->stats.dead++;

    simulator->medComponents[index].isInfected = false;
    simulator->medComponents[index].isDead = true;
    simulator->medComponents[index].isMobile = false;
}

void hospitalizeEvent(SpreadSimulator* simulator, int index)
{
    simulator->stats.hospitalized++;

    simulator->medComponents[index].survivalChance = 90;
    simulator->medComponents[index].isInHospital = true;
    simulator->medComponents[index].isMobile = false;
}

void dehospitalizeEvent(SpreadSimulator* simulator, int index)
{
    simulator->stats.hospitalized--;
    simulator->medComponents[index].isInHospital = false;
    simulator->medComponents[index].isMobile = true;
}

void cleanup(SpreadSimulator* simulator)
{
    free(simulator->positions);
    free(simulator->velocities);
    free(simulator->accelerations);
    free(simulator->medComponents);
    free(simulator->centralLocations);
}

