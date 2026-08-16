# Running Redis 8.10 as a Windows Service

`--service-install`, `--service-start`, `--service-stop`,
`--service-uninstall`, `--service-name`, and `--service-run` are the supported
SCM interface. They match the 5.0 CLI.

The 5.0 **MSI / Chocolatey** installers are historical. This 8.10 program
ships a zip (when released); MSI and Chocolatey are out of scope. If you have
an old 5.0 MSI, its `redis.windows-service.conf` + Services MMC workflow does
not apply to these 8.10 binaries.

Service commands talk to the Service Control Manager and need an elevated
token. From a non-elevated prompt Redis relaunches itself (`runas`) and a UAC
dialog may appear.

## Installing

`--service-install` **must be `argv[1]`**. Arguments after it become the
service command line, with `--service-install` rewritten to `--service-run`.
The service is Autostart as `NT AUTHORITY\NetworkService`. Install does
**not** start the service.

```
redis-server --service-install redis.windows-service.conf --loglevel notice
```

On success Redis registers the Event Log source and grants NetworkService
read/write on the executable directory, `--dir`, the conf directory, and the
logfile directory.

## Uninstalling

Does not stop the service first.

```
redis-server --service-uninstall
```

## Starting / stopping

```
redis-server --service-start
redis-server --service-stop
```

You can also use `services.msc` or `sc.exe`.

## Naming

`--service-name name` targets a specific service (default `Redis`). Put it
after the verb when installing extra arguments:

```
redis-server --service-install --service-name redisService1 --port 10001 redis.windows-service.conf
redis-server --service-start --service-name redisService1
```

## `--service-run`

Used only by the SCM ImagePath. Do not run it from a console; it exits with
error 1063 (`StartServiceCtrlDispatcher` failed) if the process was not
started by the service controller.

The worker process sets its current directory to the executable directory
(NetworkService’s default directory is `%SystemRoot%\System32`, which cannot
hold the QFork mapping).

`daemonize yes` is still a no-op; the service *is* the Windows equivalent.

## Event Log

Install registers source `redis`. A service process enables Event Log even
if the conf leaves `syslog-enabled` off. See
[Redis on Windows.md](Redis%20on%20Windows.md).
