# Oz Arena screenshots of the rebuilt railgun, with no one at the controls:
#   powershell -File shots_railgun.ps1 -out <folder> [-exe <OzArena.exe>] [-color 4]
# Runs the built game (build/engine, not the installed one) with its own home folder (so it shares no config or log
# with a game already running), Remodeled weapons and Modern player models on, on q3dm17 (a devmap, for the cheat
# commands): first person idle (a few frames apart: the cells breathe, the strips pulse) and just after a shot (the
# rail color drains and comes back), with id's gun settings and with Tony's (fov 130, gun forward 2.5, height 5,
# offset 15); third person close up from six sides and two close side views; the pickup at its spawn (2432 64 272)
# from a spectator's free camera. color1 is id's bit field: 1 blue, 2 green, 4 red. Closes only the game it started.
# Same approach as tools/weapon_swap/shots.ps1 and ../rebuild/shots_rocketl.ps1.
param([string]$out = "", [string]$exe = "", [int]$color = 4, [string]$model = "doom", [int]$timeout = 300)
if (-not $exe) { $exe = Join-Path $env:USERPROFILE "Documents\Q3Remaster\build\engine\OzArena.exe" }
$dir = Split-Path $exe
$homep = Join-Path $env:TEMP "oz_railgun_shots"
$oz = Join-Path $homep "oz"
New-Item -ItemType Directory -Force "$oz\screenshots" | Out-Null
Remove-Item "$oz\screenshots\*.jpg", "$oz\qconsole.log" -ErrorAction SilentlyContinue
$shots = 0
function shot([int]$after) { $script:shots++; return @("wait $after", 'screenshotJPEG') }
$lines = @('set cg_draw2D 0', 'set cg_drawGun 1', "set color1 $color", "set model $model", 'wait 200',
           'give railgun', 'weapon 7', 'wait 200')
# first person, id's defaults: idle a few frames apart, then a shot
foreach ($i in 1..3) { $lines += shot 35 }
$lines += @('+attack', 'wait 3', '-attack')
foreach ($i in 1..2) { $lines += shot 30 }
# Tony's gun settings
$lines += @('wait 150', 'set cg_fov 130', 'set oz_gameplay_gun_preset custom', 'set oz_gameplay_gun_forward 2.5',
            'set oz_gameplay_gun_height 5', 'set oz_gameplay_gun_offset 15', 'wait 60')
foreach ($i in 1..2) { $lines += shot 40 }
$lines += @('set cg_fov 90', 'set oz_gameplay_gun_preset default', 'set oz_gameplay_gun_forward 0',
            'set oz_gameplay_gun_height 0', 'set oz_gameplay_gun_offset 0', 'wait 60')
# third person, close, all the way round, then two close side views
$lines += @('wait 100', 'set cg_thirdPerson 1', 'set cg_thirdPersonRange 38')
foreach ($ta in 0, 60, 120, 180, 240, 300) { $lines += @("set cg_thirdPersonAngle $ta"); $lines += shot 40 }
$lines += @('set cg_thirdPersonRange 22', 'set cg_thirdPersonAngle 90'); $lines += shot 40
$lines += @('set cg_thirdPersonAngle 270'); $lines += shot 40
$lines += @('set cg_thirdPerson 0', 'team spectator', 'wait 150')
# the pickup: from a free camera beside its spawn, a few frames apart (it spins)
$lines += @('edpos 2370 30 300 18 30', 'wait 60')
foreach ($i in 1..3) { $lines += shot 45 }
$lines += @('edpos 2432 -20 300 22 90', 'wait 60')
foreach ($i in 1..2) { $lines += shot 45 }
$lines += @('wait 30', 'quit')
Set-Content -Path "$oz\railgunshots.cfg" -Value $lines -Encoding ascii
$p = Start-Process -FilePath $exe -WorkingDirectory $dir -PassThru -ArgumentList '+set','fs_homepath',$homep,
  '+set','logfile','2','+set','r_fullscreen','0','+set','r_mode','-1','+set','r_customwidth','1600',
  '+set','r_customheight','900','+set','r_ozRemodeledWeapons','1','+set','r_ozModernFighters','1',
  '+set','oz_experimental_remodeled_weapons','1','+set','oz_experimental_modern_models','1',
  '+set','color1',"$color",'+set','model',$model,'+set','headmodel',$model,'+set','g_doWarmup','0',
  '+set','fraglimit','0','+set','timelimit','0','+set','bot_enable','0',
  '+devmap','q3dm17','+wait','300','+exec','railgunshots.cfg'
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
