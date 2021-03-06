$projectDir = Split-Path $PSScriptRoot -Parent

$coolqRoot = "D:\miraiOK\plugins\MiraiNative" # 修改为你的酷Q目录
$appId = Get-Content "$projectDir\app_id.txt"
$appOutDir = $args[0]

$coolqAppDevDir = "$coolqRoot\plugins" # Mirai
$dllName = "app.dll"
$dllPath = "$appOutDir\$dllName"
$jsonName = "app.json"
$jsonPath = "$projectDir\$jsonName"

Write-Host "正在拷贝插件到酷Q应用目录……"
New-Item -Path $coolqAppDevDir -ItemType Directory -ErrorAction SilentlyContinue
Copy-Item -Force $dllPath "$coolqAppDevDir\$appId.dll"

# Java 中不支持带注释的 JSON
#Copy-Item -Force $jsonPath "$coolqAppDevDir\$appId.json"
$jsonContent = Get-Content $jsonPath -Encoding UTF8
$jsonContent = $jsonContent -replace "//[^'`"]*$",""
$Utf8NoBomEncoding = New-Object System.Text.UTF8Encoding $False
[System.IO.File]::WriteAllLines("$coolqAppDevDir\$appId.json", $jsonContent, $Utf8NoBomEncoding)

Write-Host "拷贝完成" -ForegroundColor Green
