# Shared destructive-path guard for fresh-walk.ps1 and its adversarial tests.
# Keep this file ASCII-only for Windows PowerShell 5.1.

if (-not ("MobaFreshWalkNative" -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

public static class MobaFreshWalkNative {
    private const uint DeleteAccess = 0x00010000;
    private const uint FileReadAttributes = 0x00000080;
    private const uint ShareRead = 0x1;
    private const uint ShareWrite = 0x2;
    private const uint ShareDelete = 0x4;
    private const uint OpenExisting = 3;
    private const uint BackupSemantics = 0x02000000;
    private const uint OpenReparsePoint = 0x00200000;

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
    private struct FileDispositionInformation {
        [MarshalAs(UnmanagedType.Bool)]
        public bool DeleteFile;
    }

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool SetFileInformationByHandle(
        SafeFileHandle file, int informationClass,
        ref FileDispositionInformation information, uint bufferSize);

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
        // Omitting ShareDelete prevents the leased object from being renamed or
        // replaced until this handle is closed. DELETE is used for the final
        // handle-bound disposition after its children are gone.
        return OpenDirectory(path, DeleteAccess | FileReadAttributes, ShareRead | ShareWrite);
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

    public static void MarkDirectoryForDelete(SafeFileHandle handle) {
        FileDispositionInformation information = new FileDispositionInformation();
        information.DeleteFile = true;
        if (!SetFileInformationByHandle(handle, 4, ref information,
                                        (uint)Marshal.SizeOf(information)))
            throw new Win32Exception(Marshal.GetLastWin32Error());
    }

    private static ByHandleFileInformation Information(SafeFileHandle handle) {
        ByHandleFileInformation information;
        if (!GetFileInformationByHandle(handle, out information))
            throw new Win32Exception(Marshal.GetLastWin32Error());
        return information;
    }

    private static void DeleteChildrenBound(
        SafeFileHandle directoryHandle, string directoryPath,
        Action<string> afterChildValidation) {
        ByHandleFileInformation parent = Information(directoryHandle);
        const uint DirectoryAttribute = 0x10;
        const uint ReparsePointAttribute = 0x400;
        if ((parent.FileAttributes & DirectoryAttribute) == 0 ||
            (parent.FileAttributes & ReparsePointAttribute) != 0)
            throw new IOException("Cleanup parent is not a normal directory: " + directoryPath);

        // Take only a name snapshot. Every returned name is opened with
        // OPEN_REPARSE_POINT and without FILE_SHARE_DELETE before its type is
        // trusted. A vanished or sharing-conflicted entry fails closed.
        string[] children = Directory.GetFileSystemEntries(directoryPath);
        foreach (string childPath in children) {
            using (SafeFileHandle child = OpenDirectory(
                childPath, DeleteAccess | FileReadAttributes, ShareRead | ShareWrite)) {
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
                    DeleteChildrenBound(child, childPath, afterChildValidation);
                MarkDirectoryForDelete(child);
            }
        }
    }

    public static void DeleteTreeContentsBound(
        SafeFileHandle rootHandle, string rootPath,
        Action<string> afterChildValidation) {
        if (rootHandle == null || rootHandle.IsInvalid || rootHandle.IsClosed)
            throw new ArgumentException("A live root handle is required.", "rootHandle");
        DeleteChildrenBound(rootHandle, rootPath, afterChildValidation);
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
    [System.Action[string]]$AfterChildValidation = $null) {
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
            $handle, $Lease.ClonePath, $AfterChildValidation)

        if ([MobaFreshWalkNative]::DirectoryIdentity($handle) -ne $Lease.Identity) {
            throw "CloneDir identity changed during cleanup: $($Lease.ClonePath)"
        }
        [MobaFreshWalkNative]::MarkDirectoryForDelete($handle)
    } finally {
        $handle.Dispose()
    }

    if (Test-Path -LiteralPath $Lease.ClonePath) {
        throw "CloneDir remained after handle-bound cleanup: $($Lease.ClonePath)"
    }
}
