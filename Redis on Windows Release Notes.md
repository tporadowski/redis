MSOpenTech Redis on Windows 3.2 Release Notes
=============================================
--[ Redis on Windows 3.2.100 ] Release date: Jul 01 2016

 This is the first stable release of Redis on Windows 3.2.
 This release is based on antirez/redis/3.2.1 plus some Windows specific fixes.
 It has passed all the standard tests but it hasn't been tested in a production
 environment. Therefore before considering using this release in production
 make sure to test it thoroughly in your own test environment.

 Changelog:
 
 - [Portability] strtol and strtoul fixes.
 - [Fix] Possible AV during background save.
 - [Fix] Use overlapped sockets for cluster failover communication.
 - [Cleanup cleanup] Minor changes.
 - [Portability] Windows portability fixes.
 - Merged tag 3.2.1 from antirez/3.2.
 - [Setup] Removed subdir for log, the log is now saved in the main redis dir.
 - [Cleanup] Removed unused project. 
 
 
--[ Redis on Windows 3.2.000-preview ] Release date: Jun 14 2016

 This is a technical preview of Redis on Windows 3.2.
 There are still known issues/bugs, in particular there is a bug that prevents
 the cluster fail-over functionality to work properly in certain scenarios.
 This release SHOULD NOT be used in production.

Changelog:

 - Windows port of antirez/3.2.
