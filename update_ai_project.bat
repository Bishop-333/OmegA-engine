@echo off
echo Updating Visual Studio project files to use new AI system...

REM Remove old AI file references and add new ones to quake3e.vcxproj
powershell -Command "(Get-Content 'src\project\msvc2017\quake3e.vcxproj') | Where-Object {$_ -notmatch 'engine\\ai\\behavior|engine\\ai\\navigation|aas_'} | Out-File -encoding UTF8 'src\project\msvc2017\quake3e.vcxproj.tmp'"

REM Add new AI files before </ItemGroup>
powershell -Command "$content = Get-Content 'src\project\msvc2017\quake3e.vcxproj.tmp' -Raw; $newFiles = '    <ClCompile Include=""..\..\game\ai\ai_main.c"" />`r`n    <ClCompile Include=""..\..\game\ai\character\bot_character.c"" />`r`n    <ClCompile Include=""..\..\game\ai\neural\nn_core.c"" />`r`n    <ClCompile Include=""..\..\game\ai\learning\rl_ppo.c"" />`r`n    <ClCompile Include=""..\..\game\ai\learning\skill_adaptation.c"" />`r`n    <ClCompile Include=""..\..\game\ai\tactical\tactical_combat.c"" />`r`n    <ClCompile Include=""..\..\game\ai\tactical\cover_system.c"" />`r`n    <ClCompile Include=""..\..\game\ai\tactical\movement_tactics.c"" />`r`n    <ClCompile Include=""..\..\game\ai\strategic\strategic_planning.c"" />`r`n    <ClCompile Include=""..\..\game\ai\team\team_coordination.c"" />`r`n    <ClCompile Include=""..\..\game\ai\perception\ai_perception.c"" />`r`n'; $content = $content -replace '(  </ItemGroup>)', ""$newFiles`$1""; $content | Out-File -encoding UTF8 'src\project\msvc2017\quake3e.vcxproj'"

del src\project\msvc2017\quake3e.vcxproj.tmp

REM Do the same for quake3e-ded.vcxproj
powershell -Command "(Get-Content 'src\project\msvc2017\quake3e-ded.vcxproj') | Where-Object {$_ -notmatch 'engine\\ai\\behavior|engine\\ai\\navigation|aas_'} | Out-File -encoding UTF8 'src\project\msvc2017\quake3e-ded.vcxproj.tmp'"

powershell -Command "$content = Get-Content 'src\project\msvc2017\quake3e-ded.vcxproj.tmp' -Raw; $newFiles = '    <ClCompile Include=""..\..\game\ai\ai_main.c"" />`r`n    <ClCompile Include=""..\..\game\ai\character\bot_character.c"" />`r`n    <ClCompile Include=""..\..\game\ai\neural\nn_core.c"" />`r`n    <ClCompile Include=""..\..\game\ai\learning\rl_ppo.c"" />`r`n    <ClCompile Include=""..\..\game\ai\learning\skill_adaptation.c"" />`r`n    <ClCompile Include=""..\..\game\ai\tactical\tactical_combat.c"" />`r`n    <ClCompile Include=""..\..\game\ai\tactical\cover_system.c"" />`r`n    <ClCompile Include=""..\..\game\ai\tactical\movement_tactics.c"" />`r`n    <ClCompile Include=""..\..\game\ai\strategic\strategic_planning.c"" />`r`n    <ClCompile Include=""..\..\game\ai\team\team_coordination.c"" />`r`n    <ClCompile Include=""..\..\game\ai\perception\ai_perception.c"" />`r`n'; $content = $content -replace '(  </ItemGroup>)', ""$newFiles`$1""; $content | Out-File -encoding UTF8 'src\project\msvc2017\quake3e-ded.vcxproj'"

del src\project\msvc2017\quake3e-ded.vcxproj.tmp

echo Project files updated!