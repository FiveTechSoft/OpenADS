param([string]$BuildDir = "build\mssql-verify")
. "$PSScriptRoot\mssql_test_env.local.ps1"   # local, untracked; sets the connstr
& "$PSScriptRoot\..\..\$BuildDir\tests\openads_unit_tests.exe" --test-case=*mssql*connect*
exit $LASTEXITCODE
