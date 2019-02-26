# Contributing to VFS for Git

Thank you for taking the time to contribute!

### Guidelines

* [Design Decisions](#design-decisions)
* [Cross Platform Code](#cross-platform-code)
* [Tracing and Logging](#tracing-and-logging)
* [Background Threads](#background-threads)
* [Error Handling](#error-handling)
* [Testing](#testing)

### Design Decisions

### Cross Platform Code

- Platform specific code should go in GVFSPlatform
- GVFSPlatform is only for platform specific code

### Tracing and Logging

- Log full exception stacks, and include information relevant to the exception
- In GVFS.Mount, only log errors when the issue is non-retryable and will result in I/O failures or the VFS4G process shutting down

### Background Threads

- Avoid using the thread pool (and async)
- Catch all exceptions on background threads

### Error Handling

- Do not catch exceptions that are indicative of a programming\logic error (e.g. ArgumentNullException)
- Do not use exceptions for control flow
- Errors or exceptions that are non-recoverable should shut down VFS4G

### Coding Conventions

- Most C# coding style rules are covered by StyleCop
- Prefer explicit types (e.g. no var, prefer List to IList)
- Include verbs in method names (e.g. "IsActive" rather than "Active")
- Add new interfaces when it makes sense for the product, not simply for testing
- Self-commenting code, avoid comments that do not add any additional details or context
- Check for null using == rather than is
- Use nameof when appropriate

### Testing

- ExceptionExpected category
- Add new unit & functional tests when making changes
- Unit tests should not touch the real filesystem
Functional tests are black-box tests, and should not consume any VFS4G product code

For more details see [Authoring Tests](https://github.com/Microsoft/VFSForGit/blob/master/AuthoringTests.md)