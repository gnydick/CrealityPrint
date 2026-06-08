$ErrorActionPreference = 'Continue'
$log  = 'D:\cpbuild\install.log'
$done = 'D:\cpbuild\install.done'
Remove-Item $done -ErrorAction SilentlyContinue

function Log($m){
    $ts = (Get-Date).ToString('HH:mm:ss')
    Add-Content -Path $log -Value "$ts  $m"
}

function Winst($id, $override){
    Log "==== BEGIN $id ===="
    $a = @('install','--id',$id,'-e','--accept-package-agreements','--accept-source-agreements','--silent')
    if ($override) { $a += @('--override', $override) }
    & winget @a *>> $log
    Log "==== END   $id  exit=$LASTEXITCODE ===="
}

Log "########## INSTALL RUN START ##########"
Winst 'Kitware.CMake'
Winst 'Ninja-build.Ninja'
Winst 'StrawberryPerl.StrawberryPerl'
Winst 'NASM.NASM'
Winst 'Python.Python.3.12'
Winst 'Microsoft.VisualStudio.2022.BuildTools' '--quiet --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 --add Microsoft.VisualStudio.Component.Windows11SDK.22621 --add Microsoft.VisualStudio.Component.VC.CMake.Project --includeRecommended'
Log "########## INSTALL RUN DONE ##########"
Set-Content -Path $done -Value 'ok'
