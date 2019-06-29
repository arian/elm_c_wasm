/*

TODO:
- list reverse
- FIFO
- _Scheduler_step
- constructors

Global state
- _Scheduler_guid (static var in function?)
- _Scheduler_working
- procReg (GC root)
- queue (GC root)

*/

#include "types.h"
#include "../elm/fifo.h"


enum {
    TaskCtor_Succeed,
    TaskCtor_Fail,
    TaskCtor_Binding,
    TaskCtor_And_Then,
    TaskCtor_On_Error,
    TaskCtor_Receive
};

enum {
    TaskField_Value,
    TaskField_Callback,
    TaskField_Kill,
    TaskField_Task,
    TaskField_Rest
};

enum {
    ProcField_Pid,
    ProcField_Root,
    ProcField_Stack,
    ProcField_Mailbox
};

typedef Task Custom;
typedef Process Custom;

/*
process registry may be a bit redundant
When we change the Process, are there other refs we need to keep up to date?

We already have a dict of manager instances
    - On startup we gather a dict of manager info in _Platform_effectManagers
    - Then in _Platform_initialize we combine that with the app's sendToModel
    - This gives a `managers` dict of instances, in the _Platform_initialize closure
    - So if _Platform_initialize is a Closure for realz then it is the only root apart from the stackmap
    - _Platform_initialize is the Program constructor

The 'instance' of the manager is actually a Process!
It's the thing that goes into the managers dict
So the managers dict is (at least a subset of) the Process registry
If you fire off 20 HTTP requests, how many Processes do you end up with?
Are there new children processes of the Manager's main process?
    Yes.
    Task.onEffects calls Task.spawnCmd, which calls Elm.Kernel.Scheduler.spawn

What's the story with routers?
router.__selfProcess
The loop for each Effect manager closes over the router
The router remembers the _parent_ process, as does the managers dict
How is the router itself kept alive, huh, how?

So how do we keep all the processes alive, dammit?
Well I'll tell ya...

Firstly we need to make a GC root for either the managers dict
or the whole Program including its closed-over shit. We'll see.

That takes care of the parent processes for each manager
Then there's the young 'uns
Scheduler queue doesn't keep anything alive or shove it to the back,
I was wrong about that! It just handles the bottleneck
What does happen is that when it gets to the binding, it puts the process
into a callback closure. That's the only thing keeping it alive.
It's a continuation closure that the Web API process calls back when done.
That's how it stays alive.

Kill functions
--------------
    If I'm using a Dict-like registry then I can update the Dict later in an immutable way instead of mutating
    Or a linked list will be easier in C?
    Data structure perf doesn't matter unless you have hundreds/thousands of things, and we don't!
    Run through the process list, find target process by ID, clone it, modify the clone,
    patch up the list to skip old one (use mutation, it's always to an older value)
    Allow the list to get all mixed up, just search it every time.

    Um... actually...
    kill is the only case where I'm updating a _field_ of the root of the process.
    Nested, innit.

    Kill function can be a static function address as long as I pass a process ID into it.
    Address on desktop is in the read-only 'text' segment, the lowest one of all. OK for GC.
    Also OK to mutate, since it's always outside heap!!
    This means each Effect Manager can have its own kill function. Nice.

    Custom* remove_process(processId) {
        prevCons = procRegistry // GC root
        currentCons = procRegistry

        while (1) {
            if (currentCons->header == Tag_Nil) {
            LOG_ERROR("Process %d not found in registry", processId);
            return NULL;
            }
            Custom* currentProc = currentCons->head;
            if (currentProc->values[ProcField_Id] == processId) {
            prevCons->tail = currentCons->tail;
            return currentCons;
            } else {
            prevCons = currentCons
            currentCons = currentCons->tail
            continue;
            }
        }
    }

    void update_process(processId, newRoot, newStack) {
        prevCons = procRegistry
        currentCons = procRegistry

        while (1) {
            if (currentCons->header == Tag_Nil) {
            LOG_ERROR("Process %d not found in registry", processId);
            return;
            }
            Custom* currentProc = currentCons->head;
            if (currentProc->values[ProcField_Id] == processId) {
            newProc = clone(currentProc)
            newProc->values[fieldId] = newValuePtr
            procRegistry = newCons(newProc, procRegistry)
            prevCons->tail = currentCons->tail  // procRegistry now skips currentCons
            return;
            } else {
            prevCons = currentCons
            currentCons = currentCons->tail
            continue;
            }
        }
    }

*/

Closure Scheduler_succeed; // Task.succeed
void* succeed_eval(void* args[]);

Closure Scheduler_fail(void* error); // Task.fail
void* fail_eval(void* args[]);

Closure Scheduler_andThen; // Task.andThen
void* andThen_eval(void* args[]);

Closure Scheduler_onError; // Task.onError
void* onError_eval(void* args[]);

// never called from Elm code, only Kernel
// the callback can have closed-over values inside it (e.g. its Process)
// Therefore it's not a C function but a Closure
Task* Scheduler_binding(Closure*);

// Receive is only called from _Platform_instantiateManager, in manager's event loop
// It gets passed into A2 as a value so has to be a Closure
// It takes a callback that contains closed-over values, and turns messages into Tasks
Closure Scheduler_receive;
void* receive_eval(void* args[]);

// Called from:
//   _Platform_instantiateManager
//   _Browser_on
//   _Http_configureProgress
//   _Time_setInterval
// it's a constructor so it CAN_THROW
Process* Scheduler_rawSpawn(Task* task);

Closure Scheduler_spawn; // Process.spawn
void* spawn_eval(void* args[])

// Called only from Scheduler_send and Platform_dispatchEffects
// Never curried or passed as value
// Pushes msg into Process mailbox => creates Cons => CAN_THROW
void Scheduler_rawSend(Process* proc, void* msg);

Closure Scheduler_send; // used in Platform.sendToSelf
void* send_eval(void* args[]);

Closure Scheduler_kill; // Process.kill
void* kill_eval(void* args[]);

void enqueue(Process* proc);
void step(Process* proc);
