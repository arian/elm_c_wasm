
Closure Scheduler_succeed; // Task.succeed
void* succeed_eval(void* args[]);

Closure Scheduler_fail(void* error); // Task.fail
void* fail_eval(void* args[]);

Closure Scheduler_andThen; // Task.andThen
void* andThen_eval(void* args[]);

Closure Scheduler_onError; // Task.onError
void* onError_eval(void* args[]);

// never called from Elm code, only Kernel
Task* Scheduler_binding(void (callback*)(void*));

// callback returns a Task, only called from _Platform_instantiateManager, event loop
Task* Scheduler_receive(Task* (callback*)(void* msg));

Process* Scheduler_rawSpawn(Task* task);

Closure Scheduler_spawn; // Process.spawn
void* spawn_eval(void* args[])

void Scheduler_rawSend(Process* proc, void* msg);

Closure Scheduler_send; // used in Platform.sendToSelf
void* send_eval(void* args[]);

Closure Scheduler_kill; // Process.kill
void* kill_eval(void* args[]);

void enqueue(Process* proc);
void step(Process* proc);
