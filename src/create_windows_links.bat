@echo off

set BUILD=%1

if not defined BUILD (
	set BUILD=Debug
)

echo Using configuration "%BUILD%"
set LINKNAMES=redis-benchmark redis-check-aof redis-check-rdb redis-cli redis-server

setlocal EnableDelayedExpansion

for %%n in (%LINKNAMES%) do (
	call :link %%n
)

goto :end

:link
	if exist %1 echo Removing existing %1 link
	del %1
	set LINK="..\msvs\x64\%BUILD%\%1%.exe"
	echo Creating link to %LINK%
	mklink %1 %LINK%

:end
