@{
    HostIp = '10.0.0.51'
    User   = 'darkfire'   # change if the target account is different

    ComPort = 'COM3'
    ComBaud = 115200

    KdExe  = 'C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\kd.exe'
    CdbExe = 'C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe'

    SymbolPath = 'srv*C:\symbols*https://msdl.microsoft.com/download/symbols'

    KdCmdPort = 5556
}
