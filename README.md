# A skeleton project for the ESP RTOS SDK

The goal is to have mock examples for
- project-scope include files
- additional literal data
- modules (now static, later can be git submodules)

I intend to use projects like this in a CI/CD pipeline (FIXME: under construction),
so building binaries will be triggered simply by pushing the code to git, and
the build farm can be easily scaled up if needed.

The code is just mangled from various pieces of the official SDK examples, all I did
was rearranging these pieces.

Even for a skeleton, this code is still quite in 'in progress' state, but I need
a some working code in an accessible git repo so I can start working on the builder
image, so here it is :)


