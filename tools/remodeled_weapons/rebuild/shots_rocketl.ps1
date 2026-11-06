# Oz Arena screenshots of the remodeled rocket launcher, with no one at the controls:
#   powershell -File shots_rocketl.ps1 -out <folder> [-exe <OzArena.exe>]
# Runs the built game (build/engine, not the installed one) with its own home folder (so it shares no config or log
# with a game already running), Remodeled weapons and Modern player models on, on q3dm17 (a devmap, for the cheat
# commands): first person with Tony's gun settings (fov 130, gun forward 2.5, height 5, offset 15) and with the
# defaults, a shot fired; third person close up from six sides; the pickup at its spawn (288 672 344) from a
# spectator's free camera. Closes only the game it started. Same approach as tools/weapon_swap/shots.ps1.
param([string]$out = "", [string]$exe = "", [string]$model = "doom", [int]$timeout = 240)
if (-not $exe) { $exe = Join-Path $env:USERPROFILE "Documents\Q3Remaster\build\engine\OzArena.exe" }
$dir = Split-Path $exe
$homep = Join-Path $env:TEMP "oz_rocketl_shots"
$oz = Join-Path $homep "oz"
New-Item -ItemType Directory -Force "$oz\screenshots" | Out-Null
Remove-Item "$oz\screenshots\*.jpg", "$oz\qconsole.log" -ErrorAction SilentlyContinue
$shots = 0
function shot([int]$after) { $script:shots++; return @("wait $after", 'screenshotJPEG') }
$lines = @('set cg_draw2D 0', 'set cg_drawGun 1', "set model $model", 'wait 200', 'give rocket launcher', 'weapon 5',
           'wait 200')
# first person: Tony's settings, then id's defaults
$lines += @('set cg_fov 130', 'set oz_gameplay_gun_preset custom', 'set oz_gameplay_gun_forward 2.5',
            'set oz_gameplay_gun_height 5', 'set oz_gameplay_gun_offset 15', 'wait 60')
foreach ($i in 1..2) { $lines += shot 40 }
$lines += @('+attack', 'wait 2', '-attack'); $lines += shot 6
$lines += @('wait 150', 'set cg_fov 90', 'set oz_gameplay_gun_preset default', 'set oz_gameplay_gun_forward 0',
            'set oz_gameplay_gun_height 0', 'set oz_gameplay_gun_offset 0', 'wait 60')
$lines += shot 40
# third person, close, all the way round
$lines += @('wait 100', 'set cg_thirdPerson 1', 'set cg_thirdPersonRange 38')
foreach ($ta in 0, 60, 120, 180, 240, 300) { $lines += @("set cg_thirdPersonAngle $ta"); $lines += shot 40 }
$lines += @('set cg_thirdPersonRange 22', 'set cg_thirdPersonAngle 90'); $lines += shot 40
$lines += @('set cg_thirdPersonAngle 270'); $lines += shot 40
$lines += @('set cg_thirdPerson 0', 'team spectator', 'wait 150')
# the pickup on its spawn: two sides, a few frames apart (it spins)
$lines += @('edpos 222 632 362 14 31', 'wait 60')
foreach ($i in 1..3) { $lines += shot 45 }
$lines += @('edpos 340 700 360 16 208', 'wait 60')
foreach ($i in 1..2) { $lines += shot 45 }
$lines += @('wait 30', 'quit')
Set-Content -Path "$oz\rocketlshots.cfg" -Value $lines -Encoding ascii
$p = Start-Process -FilePath $exe -WorkingDirectory $dir -PassThru -ArgumentList '+set','fs_homepath',$homep,
  '+set','logfile','2','+set','r_fullscreen','0','+set','r_mode','-1','+set','r_customwidth','1600',
  '+set','r_customheight','900','+set','r_ozRemodeledWeapons','1','+set','r_ozModernFighters','1',
  '+set','oz_experimental_remodeled_weapons','1','+set','oz_experimental_modern_models','1',
  '+set','model',$model,'+set','headmodel',$model,'+set','g_doWarmup','0','+set','fraglimit','0','+set','timelimit','0',
  '+set','bot_enable','0','+devmap','q3dm17','+wait','300','+exec','rocketlshots.cfg'
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
