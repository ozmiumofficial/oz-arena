# Oz Arena screenshots of an upgraded fighter, with no one at the controls:
#   powershell -File freecam.ps1 -bot visor -out <folder>
# Starts q3dm17 (a devmap, for the cheat commands) with that fighter as a bot, follows it, freezes it (bot_pause),
# lets it land (q3dm17 is jump pads; a bot frozen mid-air keeps falling), reads where it stands from the console log, then flies a free spectator camera around it (edpos to a spot, oz_aimat
# at the bot, screenshotJPEG), and last unfreezes it with two opponents and takes third-person shots of it fighting.
# Moves the shots to -out and removes its .cfg files from the player's oz folder.
param([string]$bot = "doom", [string]$out = "", [int]$timeout = 200)
$home_ = "$env:APPDATA\Oz Arena\oz"
$oz = "C:\Program Files (x86)\Steam\steamapps\common\Quake 3 Arena\Oz Arena"
$log = "$home_\qconsole.log"
$name = (Get-Culture).TextInfo.ToTitleCase($bot)
Set-Content -Path "$home_\fightertest.cfg" -Encoding ascii -Value @(
  'set cg_draw2D 0', 'set cg_drawGun 0', 'set cg_thirdPerson 0', "addbot $bot 2", 'wait 600',
  'team spectator', 'wait 120', "follow $name", 'wait 120', 'set bot_pause 1', 'wait 450', 'viewpos', 'wait 900',
  'exec fightercam.cfg')
Remove-Item "$home_\fightercam.cfg" -ErrorAction SilentlyContinue
$before = @(Get-ChildItem "$home_\screenshots\shot*.jpg" -ErrorAction SilentlyContinue | ForEach-Object Name)
$p = Start-Process -FilePath "$oz\OzArena.exe" -WorkingDirectory $oz -PassThru -ArgumentList '+set','logfile','2',
  '+set','g_doWarmup','0','+set','fraglimit','0','+set','timelimit','0','+devmap','q3dm17','+exec','fightertest.cfg'
Start-Sleep -Seconds 3
$pos = $null
for ($i = 0; $i -lt 90 -and -not $pos; $i++) {
  Start-Sleep -Seconds 1
  $m = Select-String -Path $log -Pattern '^\((-?\d+) (-?\d+) (-?\d+)\) : (-?\d+)' -ErrorAction SilentlyContinue | Select-Object -Last 1
  if ($m) { $g = $m.Matches[0].Groups; $pos = @([double]$g[1].Value, [double]$g[2].Value, [double]$g[3].Value, [double]$g[4].Value) }
}
if (-not $pos) { "no viewpos in the log"; Stop-Process -Id $p.Id -Force; exit 1 }
"$name's eye at $($pos -join ' ')"
$bx = $pos[0]; $by = $pos[1]; $bz = $pos[2] - 26; $yaw = $pos[3]
$lines = @('follow', 'wait 90')        # "follow" with no name stops following: a free camera where the bot stands
$count = 0
function cam($ang, $dist, $eye) {
  $a = ($yaw + $ang) * [math]::PI / 180
  $cx = [math]::Round($bx + $dist * [math]::Cos($a)); $cy = [math]::Round($by + $dist * [math]::Sin($a))
  $cz = [math]::Round($bz + $eye - 26); $cyaw = [math]::Round(($yaw + $ang + 180) % 360)
  $script:count++
  return @("edpos $cx $cy $cz 0 $cyaw", 'wait 30', 'oz_aimat', 'wait 30', 'screenshotJPEG', 'wait 15')
}
foreach ($ang in 0, 40, 80, 120, 160, 200, 240, 280, 320) { $lines += cam $ang 75 12 }   # full body, all around
foreach ($ang in 15, 345) { $lines += cam $ang 46 22 }                                  # close-ups, chest and head
foreach ($ang in 300, 60, 90) { $lines += cam $ang 34 14 }                              # the hands on the gun
$lines += cam 30 62 -10                                                                 # low, looking up
$lines += cam 330 70 48                                                                 # high, looking down
# then let it fight: third person on the moving fighter
$lines += @('set bot_pause 0', 'addbot sarge 2', 'addbot keel 2', 'wait 300', "follow $name", 'wait 60',
            'set cg_thirdPerson 1', 'set cg_thirdPersonRange 80')
foreach ($ta in 150, 110, 60, 200) {
  $lines += @("set cg_thirdPersonAngle $ta", 'wait 120', 'screenshotJPEG', 'wait 90', 'screenshotJPEG')
  $count += 2
}
$lines += @('set cg_thirdPerson 0', 'set cg_draw2D 1', 'set cg_drawGun 1')
Set-Content -Path "$home_\fightercam.cfg" -Value $lines -Encoding ascii
$new = @()
for ($i = 0; $i -lt $timeout; $i++) {
  Start-Sleep -Seconds 1
  $new = @(Get-ChildItem "$home_\screenshots\shot*.jpg" -ErrorAction SilentlyContinue | Where-Object { $before -notcontains $_.Name })
  if ($new.Count -ge $count) { break }
}
Start-Sleep -Seconds 2
Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
Remove-Item "$home_\fightertest.cfg", "$home_\fightercam.cfg" -ErrorAction SilentlyContinue
if ($out) {
  New-Item -ItemType Directory -Force $out | Out-Null
  $k = 0
  foreach ($f in ($new | Sort-Object Name)) { Move-Item $f.FullName (Join-Path $out ("{0}_{1:d2}.jpg" -f $bot, $k)) -Force; $k++ }
}
"$name`: $($new.Count) of $count screenshots"
