// scheduler.cc 
//	Routines to choose the next thread to run, and to dispatch to
//	that thread.
//
// 	These routines assume that interrupts are already disabled.
//	If interrupts are disabled, we can assume mutual exclusion
//	(since we are on a uniprocessor).
//
// 	NOTE: We can't use Locks to provide mutual exclusion here, since
// 	if we needed to wait for a lock, and the lock was busy, we would 
//	end up calling FindNextToRun(), and that would put us in an 
//	infinite loop.
//
// 	Very simple implementation -- no priorities, straight FIFO.
//	Might need to be improved in later assignments.
//
// Copyright (c) 1992-1996 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "debug.h"
#include "scheduler.h"
#include "main.h"


int Timecmp(Thread* t1,Thread* t2){ // comparison of approximate burst time.
    return (t1->predictTime >= t2->predictTime) ? 1 : -1; // shorter predict time do first 
}

int Pricmp(Thread* t1,Thread* t2){ // comparison of the priority of the thread
    return (t1->priority <= t2->priority) ? 1 : -1; // higher priority do first 
}
//----------------------------------------------------------------------
// Scheduler::Scheduler
// 	Initialize the list of ready but not running threads.
//	Initially, no ready threads.
//----------------------------------------------------------------------

Scheduler::Scheduler()
{ 
    L1 = new SortedList<Thread *>(Timecmp);
    L2 = new SortedList<Thread *>(Pricmp);
    L3 = new List<Thread *>; 
    toBeDestroyed = NULL;
} 

//----------------------------------------------------------------------
// Scheduler::~Scheduler
// 	De-allocate the list of ready threads.
//----------------------------------------------------------------------

Scheduler::~Scheduler()
{ 
    delete L1; 
    delete L2; 
    delete L3; 
} 

//----------------------------------------------------------------------
// Scheduler::ReadyToRun
// 	Mark a thread as ready, but not running.
//	Put it on the ready list, for later scheduling onto the CPU.
//
//	"thread" is the thread to be put on the ready list.
//----------------------------------------------------------------------

void
Scheduler::ReadyToRun (Thread *thread)
{
    ASSERT(kernel->interrupt->getLevel() == IntOff);
    DEBUG(dbgThread, "Putting thread on ready list: " << thread->getName());
	//cout << "Putting thread on ready list: " << thread->getName() << endl ;
    thread->setStatus(READY);
    thread->ReadyStartTime = kernel->stats->totalTicks;
    //L1
    if (thread->priority < 150 && thread->priority >= 100){
        DEBUG('z' , "[A] Tick [" << kernel->stats->totalTicks << "]: Thread [" << thread->getID() << "] is inserted into queue L[1]");
        L1->Insert(thread);
    }
    //L2
    else if (thread->priority < 100 && thread->priority >= 50) {     
        DEBUG('z' , "[A] Tick [" << kernel->stats->totalTicks << "]: Thread [" << thread->getID() << "] is inserted into queue L[2]");
        L2->Insert(thread);
    }
    //L3 
    else if (thread->priority < 50) {
        DEBUG('z' , "[A] Tick [" << kernel->stats->totalTicks << "]: Thread [" << thread->getID() << "] is inserted into queue L[3]");
        L3->Append(thread);
    }
}

void Scheduler::agingCheck(){
    aging(L1);
    aging(L2);
    aging(L3);
}

void Scheduler::doPreemptL1(){
    preemptL1(L1);
}

void Scheduler::preemptL1(List<Thread *> *list ){
    ListIterator<Thread*> *iter = new ListIterator<Thread*>((List<Thread*>*)list);
    for(;iter->IsDone() != true; iter->Next()){
        Thread* iterThread = iter->Item();
        if(kernel->currentThread->priority >=100 && kernel->currentThread->predictTime > iterThread->predictTime){
            kernel->alarm->preemptive = true;
        }
    }
}

void Scheduler::aging(List<Thread *> *list ){
    
    ListIterator<Thread*> *iter = new ListIterator<Thread*>((List<Thread*>*)list);

    for(;iter->IsDone() != true; iter->Next()){
        Thread* iterThread = iter->Item();
        int total =  kernel->stats->totalTicks - iterThread->ReadyStartTime + iterThread->TimeInReadyQueue;
        int oldPriority = iterThread->priority;
        if((oldPriority >= 0 && oldPriority < 150) && total >= 1500){
            iterThread->TimeInReadyQueue = total - 1500;
            iterThread->ReadyStartTime = kernel->stats->totalTicks;
            iterThread->priority = (oldPriority+10>149) ? 149 : oldPriority+10;
            DEBUG('z' ,"[C] Tick [" << kernel->stats->totalTicks << "]: Thread [" <<  iterThread->getID()  << "] changes its priority from[" << oldPriority << "] to [" << iterThread-> priority <<"]");
            list->Remove(iterThread);
            if(iterThread->priority >= 100){
                L1->Insert(iterThread);
                if(list != L1){
                    DEBUG('z',"[B] Tick [" << kernel->stats->totalTicks << "]: Thread [" << iterThread->getID()  << "] is removed from queue L[2]");
                    DEBUG('z' ,"[A] Tick [" << kernel->stats->totalTicks << "]: Thread [" << iterThread->getID()  << "] is inserted into queue L[1]");
                }
                if(kernel->currentThread->priority < 100){
                    kernel->alarm->preemptive = true;
                }else if(kernel->currentThread->priority >= 100 && kernel->currentThread->predictTime > iterThread->predictTime){
                    kernel->alarm->preemptive = true;
                }
            }else if(iterThread->priority >= 50){
                L2->Insert(iterThread);
                if(list!=L2){
                    DEBUG('z', "[B] Tick [" << kernel->stats->totalTicks << "]: Thread [" << iterThread->getID()  << "] is removed from queue L[3]");
                    DEBUG('z', "[A] Tick [" << kernel->stats->totalTicks << "]: Thread [" << iterThread->getID()  << "] is inserted into queue L[2]");
                }
                if(kernel->currentThread->priority < 50){
                    kernel->alarm->preemptive = true;
                }
            }else{
                L3->Append(iterThread);
            }
        }
    }
}

//----------------------------------------------------------------------
// Scheduler::FindNextToRun
// 	Return the next thread to be scheduled onto the CPU.
//	If there are no ready threads, return NULL.
// Side effect:
//	Thread is removed from the ready list.
//----------------------------------------------------------------------

Thread *
Scheduler::FindNextToRun ()
{
    Thread *thread = NULL;
    ASSERT(kernel->interrupt->getLevel() == IntOff);
    if(!L1->IsEmpty()){
        thread = L1->RemoveFront();
        thread->TimeInReadyQueue += (kernel->stats->totalTicks - thread->ReadyStartTime);
        DEBUG('z', "[B] Tick [" << kernel->stats->totalTicks << "]: Thread [" << thread->getID() << "] is remove from queue L[1]");
    }else if(!L2->IsEmpty()){
        thread = L2->RemoveFront();    
        thread->TimeInReadyQueue += (kernel->stats->totalTicks - thread->ReadyStartTime);
        DEBUG('z', "[B] Tick [" << kernel->stats->totalTicks << "]: Thread [" << thread->getID() << "] is remove from queue L[2]");
    }else if(!L3->IsEmpty()){
        thread = L3->RemoveFront();
        thread->TimeInReadyQueue += (kernel->stats->totalTicks - thread->ReadyStartTime);
        DEBUG('z', "[B] Tick [" << kernel->stats->totalTicks << "]: Thread [" << thread->getID() << "] is remove from queue L[3]");
    }else{
        thread = NULL;
    }
    return thread;
}

//----------------------------------------------------------------------
// Scheduler::Run
// 	Dispatch the CPU to nextThread.  Save the state of the old thread,
//	and load the state of the new thread, by calling the machine
//	dependent context switch routine, SWITCH.
//
//      Note: we assume the state of the previously running thread has
//	already been changed from running to blocked or ready (depending).
// Side effect:
//	The global variable kernel->currentThread becomes nextThread.
//
//	"nextThread" is the thread to be put into the CPU.
//	"finishing" is set if the current thread is to be deleted
//		once we're no longer running on its stack
//		(when the next thread starts running)
//----------------------------------------------------------------------

void
Scheduler::Run (Thread *nextThread, bool finishing)
{
    Thread *oldThread = kernel->currentThread;
    
    ASSERT(kernel->interrupt->getLevel() == IntOff);

    if (finishing) {	// mark that we need to delete current thread
         ASSERT(toBeDestroyed == NULL);
	 toBeDestroyed = oldThread;
    }
    
    if (oldThread->space != NULL) {	// if this thread is a user program,
        oldThread->SaveUserState(); 	// save the user's CPU registers
	    oldThread->space->SaveState();
    }
    
    oldThread->CheckOverflow();		    // check if the old thread
					    // had an undetected stack overflow

    kernel->currentThread = nextThread;  // switch to the next thread
    nextThread->setStatus(RUNNING);      // nextThread is now running
    
    
    nextThread->StartTime = kernel->stats->totalTicks;
    
    DEBUG(dbgThread, "Switching from: " << oldThread->getName() << " to: " << nextThread->getName());
    
    // This is a machine-dependent assembly language routine defined 
    // in switch.s.  You may have to think
    // a bit to figure out what happens after this, both from the point
    // of view of the thread and from the perspective of the "outside world".

    SWITCH(oldThread, nextThread);

    // we're back, running oldThread
      
    // interrupts are off when we return from switch!
    ASSERT(kernel->interrupt->getLevel() == IntOff);

    DEBUG(dbgThread, "Now in thread: " << oldThread->getName());

    CheckToBeDestroyed();		// check if thread we were running
					// before this one has finished
					// and needs to be cleaned up
    
    if (oldThread->space != NULL) {	    // if there is an address space
        oldThread->RestoreUserState();     // to restore, do it.
	    oldThread->space->RestoreState();
    }
}

//----------------------------------------------------------------------
// Scheduler::CheckToBeDestroyed
// 	If the old thread gave up the processor because it was finishing,
// 	we need to delete its carcass.  Note we cannot delete the thread
// 	before now (for example, in Thread::Finish()), because up to this
// 	point, we were still running on the old thread's stack!
//----------------------------------------------------------------------

void
Scheduler::CheckToBeDestroyed()
{
    if (toBeDestroyed != NULL) {
        delete toBeDestroyed;
	toBeDestroyed = NULL;
    }
}
 
//----------------------------------------------------------------------
// Scheduler::Print
// 	Print the scheduler state -- in other words, the contents of
//	the ready list.  For debugging.
//----------------------------------------------------------------------
void
Scheduler::Print()
{
    cout << "Ready list contents:\n";
    //readyList->Apply(ThreadPrint);
}
