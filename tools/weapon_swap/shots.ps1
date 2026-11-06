# Oz Arena screenshots of a remodeled weapon, with no one at the controls:
#   powershell -File shots.ps1 -out <folder> [-exe <OzArena.exe>] [-color 1]
# Runs the built game (build/engine, not the installed one) with its own home folder (so it shares no config or
# log with a game already running), Remodeled weapons and Modern player models on, on q3dm17 (a devmap, for the
# cheat commands): the railgun in first person, idle, several frames apart (the trim's pulse), and just after a
# shot (the rail color drains and comes back); held by a fighter in third person; then on the floor at its spawn
# from a spectator's free camera (the pickup's spin and bob). Closes only the game it started.
param([string]$out = "", [string]$exe = "", [int]$color = 1, [string]$model = "doom", [int]$timeout = 240,
      [string]$gunpos = "")   # "x,y,z;x,y,z;...": first-person shots with cg_gunX/Y/Z at each, to place the gun
$repo = Split-Path (Split-Path $PSScriptRoot)
if (-not $exe) { $exe = Join-Path $env:USERPROFILE "Documents\Q3Remaster\build\engine\OzArena.exe" }
$dir = Split-Path $exe
$homep = Join-Path $env:TEMP "oz_weapon_shots"
$oz = Join-Path $homep "oz"
New-Item -ItemType Directory -Force "$oz\screenshots" | Out-Null
Remove-Item "$oz\screenshots\*.jpg", "$oz\qconsole.log" -ErrorAction SilentlyContinue
$shots = 0
function shot([int]$after) { $script:shots++; return @("wait $after", 'screenshotJPEG') }
$lines = @('set cg_draw2D 0', 'set cg_drawGun 1', "set color1 $color", "set model $model", 'wait 200',
           'give railgun', 'weapon 7', 'wait 200')
if ($gunpos) {
  foreach ($g in $gunpos.Split(';')) {
    $x, $y, $z = $g.Split(',')
    $lines += @("set cg_gunX $x", "set cg_gunY $y", "set cg_gunZ $z"); $lines += shot 40
  }
  $lines += @('set cg_gunX 0', 'set cg_gunY 0', 'set cg_gunZ 0')
}
# first person, idle: the drift and the pulse, a few frames apart
foreach ($i in 1..8) { $lines += shot 35 }
# a shot, then the drain and the recharge
$lines += @('+attack', 'wait 3', '-attack')
foreach ($i in 1..3) { $lines += shot 40 }
# third person, held by the fighter
$lines += @('wait 200', 'set cg_thirdPerson 1', 'set cg_thirdPersonRange 55')
foreach ($ta in 90, 150, 210, 270, 330) { $lines += @("set cg_thirdPersonAngle $ta"); $lines += shot 40 }
$lines += @('set cg_thirdPerson 0', 'team spectator', 'wait 150')
# the pickup: from a free camera beside its spawn (2432 64 272 on q3dm17), a few frames apart (the spin)
$lines += @('edpos 2370 30 300 18 30', 'wait 60')
foreach ($i in 1..4) { $lines += shot 45 }
$lines += @('edpos 2432 -20 300 22 90', 'wait 60')
foreach ($i in 1..2) { $lines += shot 45 }
$lines += @('wait 30', 'quit')
Set-Content -Path "$oz\weaponshots.cfg" -Value $lines -Encoding ascii
$p = Start-Process -FilePath $exe -WorkingDirectory $dir -PassThru -ArgumentList '+set','fs_homepath',$homep,
  '+set','logfile','2','+set','r_fullscreen','0','+set','r_mode','-1','+set','r_customwidth','1600',
  '+set','r_customheight','900','+set','r_ozRemodeledWeapons','1','+set','r_ozModernFighters','1',
  '+set','oz_experimental_remodeled_weapons','1','+set','oz_experimental_modern_models','1',
  '+set','color1',"$color",'+set','model',$model,'+set','headmodel',$model,'+set','g_doWarmup','0','+set','fraglimit','0','+set','timelimit','0','+set','bot_enable','0',
  '+devmap','q3dm17','+wait','300','+exec','weaponshots.cfg'
$n = 0
for ($i = 0; $i -lt $timeout -and -not $p.HasExited; $i++) {
  Start-Sleep -Seconds 1
  $n = @(Get-ChildItem "$oz\screenshots\*.jpg" -ErrorAction SilentlyContinue).Count
  if ($n -ge $shots) { Start-Sleep -Seconds 3; break }
}
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
"$n of $shots screenshots"
if ($out) {
  New-Item -ItemType Directory -Force $out | Out-Null
  $k = 0
  foreach ($f in (Get-ChildItem "$oz\screenshots\*.jpg" | Sort-Object Name)) {
    Copy-Item $f.FullName (Join-Path $out ("shot_{0:d2}.jpg" -f $k)) -Force; $k++
  }
  Copy-Item "$oz\qconsole.log" (Join-Path $out "qconsole.log") -ErrorAction SilentlyContinue
}
