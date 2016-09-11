PatchQPF
========

A small utility to make Windows Update check for updates faster on certain types
of old hardware.

WARNING WARNING WARNING
-----------------------

It's not ready yet. It may not work at all. Don't try this at home. Or at work.
Or on any computer you care about.

Will this help me?
------------------

Look for the following symptoms:

* Task Manager, on the Processes tab, shows steady CPU usage with an instance of
  `svchost.exe`.
* Task Manager, on the Performance tab, and with "Show Kernel Times" enabled,
  shows the CPU time for `svchost.exe` being spent mostly as kernel time.
  This should look something like:
  ![](data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAXAAAABlBAMAAACmW6WQAAAAGFBMVEUAAAAAgEAA/wBAQECAgIDU0Mj/AAD////75vIiAAADZUlEQVR42u2bTW/jIBCGJ5aYew7t2aq095Wq/oJNtVcOQb3iw/ILGvnv78wAjj/yubHjRZqRhQmG8eOXF2opDbhCAxRcwRVcwRV8CA5gwVjnjJPipqARAHmEHV6iJuNGjUuAWz7sneCGD3MW3C0Mvt/tds4MwS04MDQLXPL1Lg5pIFU/rRuCAw+zUkbFLUgeS3ntMM1DccjgbduOwUUtabDuqz3Gdwb/3baHEbioa6SBPgEYucJJzDjNQ/HdgR/irPc9DmJ6uicMl+++A08+6HncMi5TE3N8ekjgSYmZYj8Bd+7ocbBJcXcZ3Ljj4rRJbtMtTlge3I7Are2schY8Uvd2lQ48f7IZfM5FOgQXZ8jMu7QmuZTKeXBxBpcurUkpj4uTszgx4ILgpzdqd8kq5zfJm9oWBAdzP7gFtz74mUFXFF86FFzBFbwE8P2NL2UD8N0KcRiDv1+Njwn4r/enx8cE/CdcCzMFh7dnx4+ZwGEjlx4u8dae5j8D982kHUOu41PBMfhJezjTHwk8jHs24CnFBumpPLf4mcGxUyR0k44+NJ6aswEwPQoyFIYEmzGDp0DqHThDCDwcPIEjZw4gV/hEGcKf2cBDUgQQO3ym4PnPBvBeSmqhPj4iUxvywwlegNjOsKQ+t0YZPI3NM+c5wwXF8/qt0rm+DE55GQdFowTuszeRibHxmCzrBYoNIEKytDKK54wBUYrecqV+HbhkmA08ZKUofxOCD3gEZ3uQT72wxlmBJurupX583ASeTDVYAbjIroLRsyj48WgGt8K4AjDkvSLq7vNO4vE04D9shyEFprO/BO43fRdLveEt4eLNvQ/dYl4bvLeZNXjt5nHpRhd7f9+OP5NVtjleu9q26dVPR7/Da7O9K15mUvyELuLfmxVEmE3x54IDrGKVU+lxLfBHFS8XHEq1ChSq+EbBn2wVVVytculd5WnxolbRXUV3FbWKKq5WUcXVKmW+qxTrcQVXcAVXcAWPUdEXWDUfUNVQFwT+RuhVxfx1VRR41QOvyrJKBq/qqqR3lSM4a16XqDiB9zQvCbyCksDjXijf4VdQklX0L6eCK7iCzwHurscE/NM9P8bgt/wsagLerhAH/f9xBVdwBV8O/N5fr3LsVowM/nXnPirRrhgZvMhQcAVXcAWfN/4CANq+iNxJ+KoAAAAASUVORK5CYII=)

Best results seem to occur on hardware released ~2010 or earlier.

Note
----

This code is so fresh that you should probably
[drop me a line](mailto:dmo2118%20%d0%90%d0%a2%20gmail.com) whether or not
it works, and let me know what version of Windows you have, and whether it's a
32 or 64-bit installation. And if something goes wrong, I'd also like to know
what's you've got in the "System devices" category in Device Manager. (Protip:
take a screen shot in Windows with the Print Screen key, and paste the image
with Ctrl-V into Paint.)

How it works
------------

Windows Update, while checking for updates, calls
[`QueryPerformanceFrequency`](https://msdn.microsoft.com/en-us/library/windows/desktop/ms644905%28v=vs.85%29.aspx)
constantly. `QueryPerformanceFrequency` in turn calls
[`NtQueryPerformanceCounter`](https://msdn.microsoft.com/en-us/library/bb432384%28v=vs.85%29.aspx), 
which retrieves both the time and the frequency. But reading this particular
system time source can be unusually slow depending on hardware and the version
of Windows in use -- see Microsoft's
[tale of woe](https://msdn.microsoft.com/en-us/library/windows/desktop/dn553408%28v=vs.85%29.aspx).
This in turn lengthens the already extraordinarily long Windows Update check by
approximately 2-4x, depending on hardware.

`PatchQPF.exe` hooks into the Windows Update process and replaces Microsoft's
`QueryPerformanceFrequency` with its own, faster implementation.

How can it be this bad? Doesn't Microsoft actually use their own software?
--------------------------------------------------------------------------

[I don't know.](https://www.drugabuse.gov/publications/research-reports/inhalants/how-can-inhalant-abuse-be-recognized)

Building from source
--------------------

For now, [MSYS2](https://msys2.github.io/) is required. Once you have that, from
a MinGW-w64 Win32 Shell, type `mingw32-make` from the source code root.

License
-------

PatchQPF is copyright (C) 2016 Dave Odell <dmo2118@gmail.com>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, version 3.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
