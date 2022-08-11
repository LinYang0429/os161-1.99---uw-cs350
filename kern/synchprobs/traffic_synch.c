#include <types.h>
#include <lib.h>
#include <synchprobs.h>
#include <synch.h>
#include <opt-A1.h>
#include <array.h>
/* 
 * This simple default synchronization mechanism allows only vehicle at a time
 * into the intersection.   The intersectionSem is used as a a lock.
 * We use a semaphore rather than a lock so that this code will work even
 * before locks are implemented.
 */

/* 
 * Replace this default synchronization mechanism with your own (better) mechanism
 * needed for your solution.   Your mechanism may use any of the available synchronzation
 * primitives, e.g., semaphores, locks, condition variables.   You are also free to 
 * declare other global variables if your solution requires them.
 */

/*
 * replace this with declarations of any synchronization and other variables you need here
 */
static struct lock *intersectionLock;
static struct cv *intersectionCV;

typedef struct Vehicles
{
	Direction origin;
	Direction destination;
} Vehicle;
struct array *vehicles;
bool right_turn(Vehicle *v);
bool check_constraints(Vehicle *v1, Vehicle *v2);
bool intersection_constraints(Vehicle *v);

bool
right_turn(Vehicle *v) 
{
  KASSERT(v != NULL);
  if (((v->origin == west) && (v->destination == south)) ||
      ((v->origin == south) && (v->destination == east)) ||
      ((v->origin == east) && (v->destination == north)) ||
      ((v->origin == north) && (v->destination == west))) {
    return true;
  } else {
    return false;
  }
}

bool
check_constraints(Vehicle *v1, Vehicle *v2)
{
	return (v1->origin == v2->origin) 
		|| (v1->origin == v2->destination && v1->destination == v2->origin )
		|| (v1->destination != v2->destination && (right_turn(v1) || right_turn(v2)));
}

bool
intersection_constraints(Vehicle *v)
{
	for(unsigned int i = 0; i < array_num(vehicles); i++)
	{
		if(!check_constraints(v, array_get(vehicles, i)))
		{
			return true;
		}
	}
	array_add(vehicles, v, NULL);
	return false;
}

/* 
 * The simulation driver will call this function once before starting
 * the simulation
 *
 * You can use it to initialize synchronization and other variables.
 * 
 */
void
intersection_sync_init(void)
{
  /* replace this default implementation with your own implementation */
  
	intersectionLock = lock_create("intersectionLock");
	intersectionCV = cv_create("intersectionCV");
	vehicles = array_create();
	if (intersectionCV == NULL || intersectionLock == NULL) {
		panic("could not create intersection cv or lock");
  	}
  	return;
}

/* 
 * The simulation driver will call this function once after
 * the simulation has finished
 *
 * You can use it to clean up any synchronization and other variables.
 *
 */
void
intersection_sync_cleanup(void)
{
  /* replace this default implementation with your own implementation */
	KASSERT(intersectionLock != NULL && intersectionCV != NULL && array_num(vehicles) == 0);
	array_destroy(vehicles);
	lock_destroy(intersectionLock);
	cv_destroy(intersectionCV);
}


/*
 * The simulation driver will call this function each time a vehicle
 * tries to enter the intersection, before it enters.
 * This function should cause the calling simulation thread 
 * to block until it is OK for the vehicle to enter the intersection.
 *
 * parameters:
 *    * origin: the Direction from which the vehicle is arriving
 *    * destination: the Direction in which the vehicle is trying to go
 *
 * return value: none
 */

void
intersection_before_entry(Direction origin, Direction destination) 
{
  /* replace this default implementation with your own implementation */
	KASSERT(intersectionLock != NULL && intersectionCV != NULL && vehicles != NULL);
	lock_acquire(intersectionLock);
	Vehicle *v = kmalloc(sizeof(Vehicle));
	KASSERT(v != NULL);
	v->origin = origin;
	v->destination = destination;
	while(intersection_constraints(v))
	{
		cv_wait(intersectionCV, intersectionLock);
	}
	lock_release(intersectionLock);
}


/*
 * The simulation driver will call this function each time a vehicle
 * leaves the intersection.
 *
 * parameters:
 *    * origin: the Direction from which the vehicle arrived
 *    * destination: the Direction in which the vehicle is going
 *
 * return value: none
 */

void
intersection_after_exit(Direction origin, Direction destination) 
{
  /* replace this default implementation with your own implementation */
	KASSERT(intersectionLock != NULL && intersectionCV != NULL && vehicles != NULL);
        lock_acquire(intersectionLock);
	for(unsigned int i = 0; i < array_num(vehicles); i++)
	{
		Vehicle *v = array_get(vehicles,i);
		if(v->origin == origin && v->destination == destination)
		{
			cv_broadcast(intersectionCV, intersectionLock);
			array_remove(vehicles, i);
			break;
		}
	} 
	lock_release(intersectionLock); 
}
