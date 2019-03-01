# Contributing to VFS for Git

Thank you for taking the time to contribute!

## Guidelines

* [Design Reviews](#design-reviews)
* [Platform Specific Code](#platform-specific-code)
* [Tracing and Logging](#tracing-and-logging)
* [Error Handling](#error-handling)
* [Background Threads](#background-threads)
* [Coding Conventions](#coding-conventions)
* [Testing](#testing)

## Design Reviews

Large new features or architectural changes should start with a design review.  

The design review process is as follows:

1. Create a pull request that contains a design document for the proposed change and assign the `design-doc` label to the pull request.
2. Use the pull request for design feedback and for iterating on the design.
3. Once the design is approved create a new issue whose description includes the final design document.  Include a link to the pull request used for discussing the design.
4. Close (without merging!) the pull request used for the design discussion.

## Platform Specific Code

- *Prefer cross-platform code to platform specific code*

  Cross-platform code is more easily re-used, and re-using code reduces the amount of code that we have to write, test, and maintain.

- *Platform specific code, and only platform specific code, should go in `GVFSPlatform`*

  When platform specific code is required, it should be placed in `GVFSPlatform` or one of the platforms it contains (e.g. `IKernelDriver`)

## Tracing and Logging

- *The "Error" logging level is reserved for non-retryable errors that result in I/O failures or the VFS4G process shutting down*

  The expectation from our customers is that when VFS4G logs an error in its log file (at the "Error" level) then either:
    * VFS4G had to shut down unexpectedly
    * VFS4G encountered an issue severe enough that user-initiated I/O would fail.

- *Log full exception stacks*

  Full exception stacks (i.e. `Exception.ToString`) provide more details than the exception message alone (`Exception.Message`) and make root causing issues easier.  
  
- *Do not display full exception stacks to the user*

  Exception call stacks are not usually actionable for the user, and they can lead users to the incorrect conclusion that VFS4G has crashed. As mentioned above, the full exception stacks *should* be included in VFS4G logs, but they should not be displayed as part of the error message provided to the user.

- *Include all relevant details when logging exceptions*

  

## Error Handling

- *Fail Fast: Errors/exceptions that are non-recoverable should shut down VFS4G immediately*
- *Do not catch exceptions that are indicative of a programming/logic error (e.g. ArgumentNullException)*
- *Do not use exceptions for control flow*
- *Provide the user with user-actionable messages whenever possible*

## Background Threads

- *Avoid using the thread pool (and avoid using async)*
- *Catch all exceptions on background threads*

## Coding Conventions

- *Most C# coding style rules are covered by StyleCop*
- *Prefer explicit types (e.g. no var, prefer List to IList)*
- *Include verbs in method names (e.g. "IsActive" rather than "Active")*
- *Add new interfaces when it makes sense for the product, not simply for testing*
- *Self-commenting code, avoid comments that do not add any additional details or context*
- *Check for null using == rather than `is`*
- *Use nameof when appropriate*
- *C++: Declare static functions at the top of .cpp files*

## Testing

- *ExceptionExpected category*
- *Add new unit & functional tests when making changes*
- *Unit tests should not touch the real filesystem
Functional tests are black-box tests, and should not consume any VFS4G product code*

For more details on writing tests see [Authoring Tests](https://github.com/Microsoft/VFSForGit/blob/master/AuthoringTests.md)