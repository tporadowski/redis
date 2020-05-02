@echo off

set CONFIGURATION=%1
if not defined CONFIGURATION (
	set CONFIGURATION=Release
)

echo Building with configuration = %CONFIGURATION% in 5 seconds
rem wait for 5 seconds... (https://stackoverflow.com/a/735603)
ping -n 6 127.0.0.1 > nul

msbuild RedisServer.sln -t:Rebuild -p:Configuration=%CONFIGURATION%;Platform=x64;Machine=x64
msbuild RedisServer.sln -t:RedisCli -p:Configuration=%CONFIGURATION%;Platform=x64;Machine=x64
msbuild RedisServer.sln -t:RedisBenchmark -p:Configuration=%CONFIGURATION%;Platform=x64;Machine=x64

cd msi
msbuild RedisMsi.sln -t:Rebuild -p:Configuration=%CONFIGURATION%;Platform=x64
cd ..
