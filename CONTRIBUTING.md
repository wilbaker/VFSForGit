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
3. Once the design is approved create a new issue whose description includes the final design document.  Include a link to the pull request that was used for discussion.
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
  
- *Do not display full exception stacks to users*

  Exception call stacks are not usually actionable for the user, and they can lead users to the incorrect conclusion that VFS4G has crashed. As mentioned above, the full exception stacks *should* be included in VFS4G logs, but they should not be displayed as part of the error message provided to the user.

- *Include all relevant details when logging exceptions*

  Sometimes an exception callstack alone is not to root cause failures in VFS4G.  When catching (or throwing) exceptions, log relevant details that will help diagnose the issue.  As a general rule, the closer an exception is caught to where it's throw, the more relevant details there will be to log.

  Example:
  ```
  catch (Exception e)
  {
    EventMetadata metadata = new EventMetadata();
    metadata.Add("Area", "Mount");
    metadata.Add(nameof(enlistmentHookPath), enlistmentHookPath);
    metadata.Add(nameof(installedHookPath), installedHookPath);
    metadata.Add("Exception", e.ToString());
    context.Tracer.RelatedError(metadata, $"Failed to compare {hook.Name} version");
  ```

## Error Handling

- *Fail Fast: Errors/exceptions that risk data loss or corruption should shut down VFS4G immediately*

  Preventing data loss and repo corruption is critical.  If errors/exceptions occur that could lead to data loss it's better to shut down VFS4G than keep the repo mounted and risk corruption.

- *Do not catch exceptions that are indicative of a programming/logic error (e.g. `ArgumentNullException`)*

  Any exceptions that result from programmer error (e.g. `ArgumentNullException`) should be caught as early in the development process as possible.  Avoid `catch` statements that would hide these errors (e.g. `catch(Exception)`) and make them more difficult to during the development process.

  The only exception to this rule is for [unhandled exceptions in background threads](#bgexceptions)

- *Do not use exceptions for normal control flow*

  Prefer to write code that avoids exceptions being thrown (e.g. the `TryXXX` pattern). There are performance costs to using exceptions for control flow, and in VFS4G we most frequently need to address errors as soon as the happen (something that exceptions make easier to avoid).
  
- *Provide the user with user-actionable messages whenever possible*

  When a failures occur and there are well-known steps to fix the issue they should be provided to the user. 

  [Example](https://github.com/Microsoft/VFSForGit/blob/9049ba48bafe30432ddb0453a23f097f85d065c7/GVFS/GVFS/CommandLine/PrefetchVerb.cs#L370):
  > "You can only specify --hydrate if the repo is mounted. Run 'gvfs mount' and try again."

## Background Threads

- *Avoid using the thread pool (and avoid using async)*

  `HttpRequestor.SendRequest` makes a [blocking call](https://github.com/Microsoft/VFSForGit/blob/4baa37df6bde2c9a9e1917fc7ce5debd653777c0/GVFS/GVFS.Common/Http/HttpRequestor.cs#L135) to `HttpClient.SendAsync` and that blocking call consumes a thread from the managed thread pool. Until that design changes the rest of VFS4G must avoid using the thread pool unless absolutely necessary.  If the thread pool is required, any long running tasks should be moved to a separate thread that's managed by VFS4G itself (see [GitMainteanceQueue](https://github.com/Microsoft/VFSForGit/blob/4baa37df6bde2c9a9e1917fc7ce5debd653777c0/GVFS/GVFS.Common/Maintenance/GitMaintenanceQueue.cs#L19) for an example).

  Testing has shown that if long-running (or blocking) work is scheduled for the managed thread pool it will have a detrimental effect on `HttpRequestor.SendRequest` and any operation that depends on it (e.g. downloading file sizes, downloading loose objects, hydrating files, etc.).

- <a id="bgexceptions"></a>*Catch all exceptions on long-running tasks and background threads*

  It's not safe to allow VFS4G to run continue to run after unhandled exceptions in long running tasks (`TaskCreationOptions.LongRunning`) and/or background threads have causes those tasks/threads to stop running.  A top level `try/catch(Exception)` should wrap the code that runs in the background thread, and any unhandled exceptions should be caught, logged, and force VFS4G to exit (`Environment.Exit`).

  An example of this pattern can be seen [here](https://github.com/Microsoft/VFSForGit/blob/4baa37df6bde2c9a9e1917fc7ce5debd653777c0/GVFS/GVFS.Virtualization/Background/BackgroundFileSystemTaskRunner.cs#L233) in `BackgroundFileSystemTaskRunner.ProcessBackgroundTasks`.

## Coding Conventions

- *Most C# coding style rules are covered by StyleCop*
- *Prefer explicit types to interfaces (or implicitly typed variables)*
  * var
  * auto
  * IList

- *Include verbs in method names (e.g. "IsActive" rather than "Active")*
- *Add new interfaces when it makes sense for the product, not simply for testing*
- *Self-commenting code, avoid comments that do not add any additional details or context*
- *Check for `null` using the equality (`==`) and inequality (`!=`) operators rather than `is`*

  A corollary to this guideline is that equality/inequality operators that break `null` checks should not be added (see [this post](https://stackoverflow.com/questions/40676426/what-is-the-difference-between-x-is-null-and-x-null) for an example).

- *Use `nameof(...)` rather than hardcoded strings*

### C/C++

- *Prefer C++ casts to C casts*

  C++ style casts (e.g. `static_cast<T>`) more clearly express the intent of the programmer and allow for better validation by the compiler.

- *Declare static functions at the top of source files*
  
  This ensures that the functions can be called from anywhere inside the file.

- *Do not use namespace `using` statements in header files*

  `using` statements inside header files are picked up by all source files that include the headers, and can cause unexpected errors if there are name collisions.

- *Prefer `using` to full namespaces in source files*

  Example:
  ```
  // Inside MyFavSourceFile.cpps
  
  // Prefer using std::string
  using std::string;
  static string s_myString;

  // To the full std::string namespace
  std::string s_myString;
  ```

- *Use a meaningful prefix for "public" free functions, and use the same prefix for all functions in a given header file*

   Example:
   ```
   // Functions declared in VirtualizationRoots.h have "VirtualizationRoot_" prefix
   bool VirtualizationRoot_IsOnline(VirtualizationRootHandle rootHandle);

   // Static helper function in VirtualizationRoots.cpp has no prefix
   static VirtualizationRootHandle FindOrDetectRootAtVnode(vnode_t vnode, const FsidInode& vnodeFsidInode);
   ```

## Testing

- *Add the `ExceptionExpected` attribute to unit tests that include thrown exceptions*

  Example:
  ```
  [TestCase]
  [Category(CategoryConstants.ExceptionExpected)]
  public void ParseFromLsTreeLine_NullRepoRoot()
  ```
  
  Unit tests tagged with `ExceptionExpected` are not executed when the unit tests are run with a debugger attached.  This attribute prevents developers from having to keep continuing the unit tests each time the debugger catches an expected exception.

- *Add new unit & functional tests when making changes*

  Comprehensive tests are essential for maintaining the health and quality of the product.

- *Use a `mock` prefix for absolute file system paths and URLs*

  The unit tests should not touch the real file system nor should they reach out to any real URLs. By using  `mock:\\` and `mock://` for paths/URLs it ensures that any product code that is (incorrectly) not using a mock will not interact with the real file system or attempt to contact a real URL.

- *Functional tests are black-box tests, and should not consume any VFS4G product code*

  Keeping the code separate helps ensure that bugs in the product code do not compromise the integrity of the functional tests. 

For more details on writing tests see [Authoring Tests](https://github.com/Microsoft/VFSForGit/blob/master/AuthoringTests.md)