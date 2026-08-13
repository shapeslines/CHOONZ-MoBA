# Shared destructive-path guard for fresh-walk.ps1 and its adversarial tests.
# Keep this file ASCII-only for Windows PowerShell 5.1.

if (-not ("MobaFreshWalkNative" -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32.SafeHandles;

public static class MobaFreshWalkNative {
    private const uint DeleteAccess = 0x00010000;
    private const uint FileReadAttributes = 0x00000080;
    private const uint GenericWrite = 0x40000000;
    private const uint ShareRead = 0x1;
    private const uint ShareWrite = 0x2;
    private const uint ShareDelete = 0x4;
    private const uint OpenExisting = 3;
    private const uint BackupSemantics = 0x02000000;
    private const uint OpenReparsePoint = 0x00200000;
    private const uint FsctlSetReparsePoint = 0x000900A4;
    private const uint IoReparseTagMountPoint = 0xA0000003;

    [StructLayout(LayoutKind.Sequential)]
    private struct FileTime {
        public uint Low;
        public uint High;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct ByHandleFileInformation {
        public uint FileAttributes;
        public FileTime CreationTime;
        public FileTime LastAccessTime;
        public FileTime LastWriteTime;
        public uint VolumeSerialNumber;
        public uint FileSizeHigh;
        public uint FileSizeLow;
        public uint NumberOfLinks;
        public uint FileIndexHigh;
        public uint FileIndexLow;
    }

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern SafeFileHandle CreateFileW(
        string path, uint desiredAccess, uint shareMode, IntPtr securityAttributes,
        uint creationDisposition, uint flagsAndAttributes, IntPtr templateFile);

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool GetFileInformationByHandle(
        SafeFileHandle file, out ByHandleFileInformation information);

    [StructLayout(LayoutKind.Sequential)]
    private struct FileDispositionInformationEx {
        public uint Flags;
    }

    [DllImport("kernel32.dll", EntryPoint = "SetFileInformationByHandle", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool SetFileDispositionInformationExByHandle(
        SafeFileHandle file, int informationClass,
        ref FileDispositionInformationEx information, uint bufferSize);

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool DeviceIoControl(
        SafeFileHandle device, uint controlCode,
        byte[] input, uint inputSize,
        IntPtr output, uint outputSize,
        out uint bytesReturned, IntPtr overlapped);

    private static SafeFileHandle OpenDirectory(string path, uint desiredAccess, uint shareMode) {
        SafeFileHandle handle = CreateFileW(
            path, desiredAccess, shareMode, IntPtr.Zero, OpenExisting,
            BackupSemantics | OpenReparsePoint, IntPtr.Zero);
        if (handle.IsInvalid) {
            int error = Marshal.GetLastWin32Error();
            handle.Dispose();
            throw new Win32Exception(error);
        }
        return handle;
    }

    public static SafeFileHandle OpenCleanupDirectory(string path) {
        // Omitting ShareWrite and ShareDelete prevents in-place reparse mutation,
        // rename, or replacement until this handle is closed. DELETE is used for
        // final handle-bound disposition after the children are gone.
        return OpenDirectory(
            path, DeleteAccess | FileReadAttributes, ShareRead);
    }

    public static string DirectoryIdentity(SafeFileHandle handle) {
        if (handle == null || handle.IsInvalid || handle.IsClosed)
            throw new ArgumentException("A live directory handle is required.", "handle");
        ByHandleFileInformation information;
        if (!GetFileInformationByHandle(handle, out information))
            throw new Win32Exception(Marshal.GetLastWin32Error());
        return String.Format("{0:X8}:{1:X8}:{2:X8}",
            information.VolumeSerialNumber,
            information.FileIndexHigh,
            information.FileIndexLow);
    }

    public static string DirectoryIdentity(string path) {
        using (SafeFileHandle handle = OpenDirectory(
            path, FileReadAttributes, ShareRead | ShareWrite | ShareDelete)) {
            return DirectoryIdentity(handle);
        }
    }

    public static void MarkObjectForDelete(SafeFileHandle handle) {
        const int FileDispositionInfoEx = 21;
        const uint FileDispositionDelete = 0x1;
        const uint FileDispositionIgnoreReadOnly = 0x10;
        FileDispositionInformationEx information = new FileDispositionInformationEx();
        information.Flags = FileDispositionDelete | FileDispositionIgnoreReadOnly;
        if (!SetFileDispositionInformationExByHandle(
                handle, FileDispositionInfoEx, ref information,
                (uint)Marshal.SizeOf(information)))
            throw new Win32Exception(Marshal.GetLastWin32Error());
    }

    private static void PutUInt16(byte[] buffer, int offset, ushort value) {
        buffer[offset] = (byte)value;
        buffer[offset + 1] = (byte)(value >> 8);
    }

    private static void PutUInt32(byte[] buffer, int offset, uint value) {
        buffer[offset] = (byte)value;
        buffer[offset + 1] = (byte)(value >> 8);
        buffer[offset + 2] = (byte)(value >> 16);
        buffer[offset + 3] = (byte)(value >> 24);
    }

    // Adversarial-test helper. It performs the real write-open and
    // FSCTL_SET_REPARSE_POINT operation that a cleanup race would need.
    public static bool TrySetMountPointForTest(
        string path, string target, out int error) {
        error = 0;
        SafeFileHandle handle = CreateFileW(
            path, GenericWrite, ShareRead | ShareWrite | ShareDelete,
            IntPtr.Zero, OpenExisting, BackupSemantics | OpenReparsePoint,
            IntPtr.Zero);
        if (handle.IsInvalid) {
            error = Marshal.GetLastWin32Error();
            handle.Dispose();
            return false;
        }

        using (handle) {
            string printName = Path.GetFullPath(target).TrimEnd('\\');
            string substituteName = "\\??\\" + printName;
            byte[] substitute = Encoding.Unicode.GetBytes(substituteName);
            byte[] print = Encoding.Unicode.GetBytes(printName);
            int pathBytes = substitute.Length + 2 + print.Length + 2;
            int dataBytes = 8 + pathBytes;
            if (substitute.Length > UInt16.MaxValue ||
                print.Length > UInt16.MaxValue ||
                dataBytes > UInt16.MaxValue) {
                error = 206; // ERROR_FILENAME_EXCED_RANGE
                return false;
            }

            byte[] buffer = new byte[8 + dataBytes];
            PutUInt32(buffer, 0, IoReparseTagMountPoint);
            PutUInt16(buffer, 4, (ushort)dataBytes);
            PutUInt16(buffer, 8, 0);
            PutUInt16(buffer, 10, (ushort)substitute.Length);
            PutUInt16(buffer, 12, (ushort)(substitute.Length + 2));
            PutUInt16(buffer, 14, (ushort)print.Length);
            Buffer.BlockCopy(substitute, 0, buffer, 16, substitute.Length);
            Buffer.BlockCopy(print, 0, buffer, 18 + substitute.Length, print.Length);

            uint returned;
            bool changed = DeviceIoControl(
                handle, FsctlSetReparsePoint, buffer, (uint)buffer.Length,
                IntPtr.Zero, 0, out returned, IntPtr.Zero);
            if (!changed) error = Marshal.GetLastWin32Error();
            return changed;
        }
    }

    private static ByHandleFileInformation Information(SafeFileHandle handle) {
        ByHandleFileInformation information;
        if (!GetFileInformationByHandle(handle, out information))
            throw new Win32Exception(Marshal.GetLastWin32Error());
        return information;
    }

    private static void DeleteChildrenBound(
        SafeFileHandle directoryHandle, string directoryPath,
        Action<string> afterChildValidation,
        Action<string> afterParentValidation) {
        ByHandleFileInformation parent = Information(directoryHandle);
        const uint DirectoryAttribute = 0x10;
        const uint ReparsePointAttribute = 0x400;
        if ((parent.FileAttributes & DirectoryAttribute) == 0 ||
            (parent.FileAttributes & ReparsePointAttribute) != 0)
            throw new IOException("Cleanup parent is not a normal directory: " + directoryPath);

        if (afterParentValidation != null)
            afterParentValidation(directoryPath);

        // Take only a name snapshot. Every returned name is opened with
        // OPEN_REPARSE_POINT and without FILE_SHARE_DELETE before its type is
        // trusted. A vanished or sharing-conflicted entry fails closed.
        string[] children = Directory.GetFileSystemEntries(directoryPath);
        foreach (string childPath in children) {
            try {
                using (SafeFileHandle child = OpenDirectory(
                    childPath, DeleteAccess | FileReadAttributes, ShareRead)) {
                    ByHandleFileInformation information = Information(child);
                    bool isDirectory = (information.FileAttributes & DirectoryAttribute) != 0;
                    bool isReparsePoint =
                        (information.FileAttributes & ReparsePointAttribute) != 0;

                    if (afterChildValidation != null)
                        afterChildValidation(childPath);

                    // Reparse points are deleted as the link object. They are never
                    // traversed. Normal directories keep their no-delete-share
                    // handle live for the complete recursive walk.
                    if (isDirectory && !isReparsePoint)
                        DeleteChildrenBound(
                            child, childPath, afterChildValidation,
                            afterParentValidation);
                    MarkObjectForDelete(child);
                }
            } catch (Exception exception) {
                throw new IOException(
                    "Handle-bound cleanup failed at '" + childPath + "': " +
                    exception.Message, exception);
            }
        }
    }

    public static void DeleteTreeContentsBound(
        SafeFileHandle rootHandle, string rootPath,
        Action<string> afterChildValidation,
        Action<string> afterParentValidation) {
        if (rootHandle == null || rootHandle.IsInvalid || rootHandle.IsClosed)
            throw new ArgumentException("A live root handle is required.", "rootHandle");
        DeleteChildrenBound(
            rootHandle, rootPath, afterChildValidation, afterParentValidation);
    }
}
'@
}

function Get-FreshWalkTrimmedPath([string]$Path) {
    return [System.IO.Path]::GetFullPath($Path).TrimEnd(
        [System.IO.Path]::DirectorySeparatorChar,
        [System.IO.Path]::AltDirectorySeparatorChar)
}

function Assert-FreshWalkLocation([string]$Path, [string]$TempRoot, [string]$SourceRoot) {
    $trimmedPath = Get-FreshWalkTrimmedPath $Path
    $trimmedTemp = Get-FreshWalkTrimmedPath $TempRoot
    $trimmedSource = Get-FreshWalkTrimmedPath $SourceRoot
    $parentInfo = [System.IO.Directory]::GetParent($trimmedPath)
    $parent = if ($null -eq $parentInfo) { '' } else { Get-FreshWalkTrimmedPath $parentInfo.FullName }

    if ($trimmedPath.Equals($trimmedTemp, [System.StringComparison]::OrdinalIgnoreCase) -or
        -not $parent.Equals($trimmedTemp, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "CloneDir must be a non-root direct child of the system temp directory: $trimmedPath"
    }
    if ($trimmedPath.Equals($trimmedSource, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "CloneDir must not be the source repository: $trimmedPath"
    }
    return $trimmedPath
}

function New-FreshWalkLease([string]$CloneDir, [string]$SourceRoot) {
    $tempRoot = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath())
    $clonePath = Assert-FreshWalkLocation $CloneDir $tempRoot $SourceRoot
    if (Test-Path -LiteralPath $clonePath) {
        $item = Get-Item -Force -LiteralPath $clonePath
        if (-not $item.PSIsContainer) {
            throw "CloneDir exists but is not a directory: $clonePath"
        }
        if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "CloneDir must not be a reparse point: $clonePath"
        }
        throw "CloneDir must not already exist: $clonePath"
    }

    $token = [Guid]::NewGuid().ToString('N')
    return [pscustomobject]@{
        ClonePath = $clonePath
        SourceRoot = [System.IO.Path]::GetFullPath($SourceRoot)
        TempRoot = $tempRoot
        MarkerName = "moba-fresh-walk-lease-$token"
        Token = $token
        Identity = $null
    }
}

function Initialize-FreshWalkLease($Lease) {
    $clonePath = Assert-FreshWalkLocation $Lease.ClonePath $Lease.TempRoot $Lease.SourceRoot
    $item = Get-Item -Force -LiteralPath $clonePath
    if (-not $item.PSIsContainer) {
        throw "CloneDir exists but is not a directory: $clonePath"
    }
    if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "CloneDir must not be a reparse point: $clonePath"
    }

    $gitDir = Join-Path $clonePath '.git'
    $gitItem = Get-Item -Force -LiteralPath $gitDir
    if (-not $gitItem.PSIsContainer -or
        ($gitItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "CloneDir does not contain a normal .git directory: $clonePath"
    }

    $markerPath = Join-Path $gitDir $Lease.MarkerName
    $stream = [System.IO.File]::Open($markerPath, [System.IO.FileMode]::CreateNew,
        [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)
    try {
        $bytes = [System.Text.Encoding]::ASCII.GetBytes($Lease.Token)
        $stream.Write($bytes, 0, $bytes.Length)
        $stream.Flush()
    } finally {
        $stream.Dispose()
    }

    $Lease.Identity = [MobaFreshWalkNative]::DirectoryIdentity($clonePath)
    Assert-FreshWalkLease $Lease
}

function Assert-FreshWalkLease($Lease, $DirectoryHandle = $null) {
    if ($null -eq $Lease -or [string]::IsNullOrEmpty($Lease.Identity)) {
        throw 'Fresh-walk cleanup lease is not initialized'
    }
    $clonePath = Assert-FreshWalkLocation $Lease.ClonePath $Lease.TempRoot $Lease.SourceRoot
    $item = Get-Item -Force -LiteralPath $clonePath
    if (-not $item.PSIsContainer) {
        throw "CloneDir exists but is not a directory: $clonePath"
    }
    if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "CloneDir must not be a reparse point: $clonePath"
    }

    $identity = if ($null -eq $DirectoryHandle) {
        [MobaFreshWalkNative]::DirectoryIdentity($clonePath)
    } else {
        [MobaFreshWalkNative]::DirectoryIdentity($DirectoryHandle)
    }
    if (-not $identity.Equals($Lease.Identity, [System.StringComparison]::Ordinal)) {
        throw "CloneDir identity changed before cleanup: $clonePath"
    }

    $markerPath = Join-Path (Join-Path $clonePath '.git') $Lease.MarkerName
    $marker = Get-Item -Force -LiteralPath $markerPath
    if ($marker.PSIsContainer -or
        ($marker.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0 -or
        [System.IO.File]::ReadAllText($markerPath, [System.Text.Encoding]::ASCII) -ne $Lease.Token) {
        throw "CloneDir cleanup marker changed: $clonePath"
    }
}

function Remove-FreshWalkLease(
    $Lease,
    [scriptblock]$AfterValidation = $null,
    [System.Action[string]]$AfterChildValidation = $null,
    [System.Action[string]]$AfterParentValidation = $null) {
    # Keep a non-delete-sharing handle on the exact leased directory from the
    # validation through final disposition. This closes the path-swap interval:
    # the root cannot be renamed or replaced while its children are removed, and
    # the root itself is deleted through the verified handle rather than by path.
    $handle = [MobaFreshWalkNative]::OpenCleanupDirectory($Lease.ClonePath)
    try {
        Assert-FreshWalkLease $Lease $handle
        if ($null -ne $AfterValidation) {
            & $AfterValidation
        }

        [MobaFreshWalkNative]::DeleteTreeContentsBound(
            $handle, $Lease.ClonePath, $AfterChildValidation,
            $AfterParentValidation)

        if ([MobaFreshWalkNative]::DirectoryIdentity($handle) -ne $Lease.Identity) {
            throw "CloneDir identity changed during cleanup: $($Lease.ClonePath)"
        }
        [MobaFreshWalkNative]::MarkObjectForDelete($handle)
    } finally {
        $handle.Dispose()
    }

    if (Test-Path -LiteralPath $Lease.ClonePath) {
        throw "CloneDir remained after handle-bound cleanup: $($Lease.ClonePath)"
    }
}
