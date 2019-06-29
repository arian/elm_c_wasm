# Project outline
- Implement Elm in WebAssembly
    - Finally chose C as an intermediate language after frustrations with direct-to-Wasm and Rust.
- The official Elm compiler can build on top of JavaScript features like Objects and first-class functions. These things don't exist in C or Wasm so we have to build them. That's what this repo does.
- Gradually build Elm's core libraries in C and write some tests to mimic generated code from user programs.

# Closures
I previously wrote a [blog post][blogpost] about how to implement Elm first-class functions in WebAssembly. The Closure data structure in [types.h](./src/kernel/types.h) is based on those ideas, although it has evolved slightly in the meantime.

In a nutshell, the Closure data structure is a value that can be passed around an Elm program. It stores up any arguments that are partially applied to it, until it is "full". It also contains a function pointer, so that when the last argument is applied, that function can be called. A working example of all of this can be found in `test_apply` in [utils_test.c](./src/test/utils_test.c).

The version in the blog post used the same number of bytes regardless of the number of closed-over values held in the Closure. The new version is a bit more compact, using only the space it needs.

[blogpost]: https://dev.to/briancarroll/elm-functions-in-webassembly-50ak


# Extensible Records
- A good intro to Elm extensible records can be found [here](https://elm-lang.org/docs/records#access). 
- In this project they are split into two C structures, `Record` and `FieldSet`, defined in [types.h](./src/kernel/types.h)
- The `Record` stores the values only. The field names are stored in a `FieldSet`, shared by all values of the same Record type.
- Field names are represented as integers. The compiler will convert every field name in the project to a unique integer, using the same kind of optimisation the Elm 0.19 compiler uses to map field names to short combinations of letters.

- Record accessor functions
    - Elm has special functions for accessing records, prefixed by a dot, like `.name`. The important thing is that this function can be applied to _any_ Record type that contains a field called `name`.
    - Implemented using a Kernel function that takes the field ID as an Elm Int, and the record itself
    ```elm
        recordAccess : Int -> r -> a
        recordAccess fieldId record =
            -- Kernel C code
            -- 1. Find the position of the `fieldId` in the record's FieldSet
            -- 2. Look up the same position in `record`
            -- 3. Return the value found
    ```
    - An accessor function for a particular field is created by partially applying the field ID in the generated code. In Elm syntax it would look something like the following:
    ```elm
        .name : { r | name : a } -> a
        .name = recordAccess 123  -- where 123 represents the field called 'name'
    ```
    - Field lookups are implemented as a binary search, which requires the fields to be pre-sorted by the Elm compiler
    - `recordAccess` is not safe if the field does not actually exist in the record, but it's not available to user Elm code, it can only be emitted by the compiler.

- Record update
    - `r2 = { r1 | field1 = newVal1, field2 = newVal2 }`
    - This Elm syntax is implemented using a C function `record_update`, found in [utils.c](./src/kernel/utils.c)
    - First it clones the original record
    - Then for each field to be updated
        - Finds the index of the field in the record's FieldSet
        - Changes the value at the same index in the Record


# SuperTypes / constrained type variables
TODO

# Boxed vs unboxed numbers
TODO

# Headers
TODO

# Padding & alignment
TODO

# Memory: stack, heap, static, registers
TODO

# Dropping type info
TODO

# Alternatives to C

## Why not Rust? OMG!!
Let me be clear, I really wanted to use Rust. It's just generally a better language. Hey, it's even heavily influenced by Haskell and ML, just like Elm!

But using Rust to implement Elm was difficult enough that I got frustrated and demotivated and stopped working on this project for a few weeks. This project is a hobby for me, so if I'm not enjoying it then it won't happen. And at some point it was either drop the project or switch to C.

Rust is _definitely_ a better language... for handwritten code. But we're talking about _generated code_ from the Elm compiler. It's different in a few ways.

Firstly, Elm's type system guarantees that a lot of potentially bad things can't happen. But the Rust compiler doesn't know that, so the extra strictness can really be a pain. We basically have to prove to Rust that some code that came out of the Elm compiler is type safe, even though we already know it is. This turned out to be tricky, and without much real benefit.

Secondly, it's well known that "fighting the borrow checker" is something that everyone learning Rust goes through. And I have never used Rust for a real project before, so I experienced it too. Apparently you eventually learn how to deal with it. But the borrow checker isn't actually designed for garbage-collected code anyway, so it's quite a lot of pain and effort with not much reward at the end!

Some of the patterns Rust encourages didn't seem to make sense to me in a garbage collected context. Rust encourages you to keep as many of your values as possible in the stack, which makes sense when you don't have a garbage collector - you want to destroy values as soon as you don't need them anymore. In Elm, a _lot_ more stuff is going to be allocated on the heap than in a normal Rust program and that should be OK.

It really felt like at every turn, I was spending all my time _fooling_ Rust into believing my code was OK, and in the process, invalidating a lot of the extra help it should give me compared to C. I also had a couple of years experience with C from about 10 years ago, so that was a factor too. But to be clear, I had no burning desire to go back to it!

## Direct to Wasm

My first approach to this project was to directly generate WebAssembly from the Elm AST using a forked version of the compiler. I wrote a set of Haskell types to model a WebAssembly module, and created a WebAssembly DSL and code generator from that.

I wrote a few of Elm's Kernel functions using the same Haskell DSL. So the Kernel code was actually written in Haskell that would generate WebAssembly.

That all worked reasonably well. I got an example Elm program working, which implemented currying and closures.

However as soon as I had my first real piece of debugging work to do, I realised it was pretty painful. That seems kind of obvious, but I'd thought the DSL would help a lot more than it did. Wasm is just so low-level I couldn't keep enough of it in my mind at one time to be useful for debugging.

However I really learned a lot about WebAssembly by doing this.


