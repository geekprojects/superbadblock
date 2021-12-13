# Super Bad Block

Super Bad Block attempts to recover data from disks that are too damaged for
fsck to fix.

SBB will find files and directory structures by brute force, ignoring the
possibly broken metadata and looking for directory and inode records directly.
These are recorded to an SQLite database, so you only have to do it once. This
database can then be used with the sbbfuse tool to mount this database as
a virtual filesystem. Reading the files will then attempt to extract the data
from the source image/disk.

This is a work in progress

## Features
* Open Source and FREE!
* Supports APFS

## Usage
1. **_Don't Panic!_** Don't attempt to fix the disk any further. It may result in further corruption.
2. Use [ddrescue](https://www.gnu.org/software/ddrescue/) to recover as much data as possible
3. Use the superbadblock tool on the copy to find as many files as possible
4. Mount the database with sbbfuse to access the files like any other filesystem.

## To Do
1. Support different File Systems (DOS, NTFS etc)
2. Make it easier to use.
3. Handle encrypted disks.
4. Build in ddrescue functionality to simplify the process.

## History ##
I started this project after my wife's old Macbook Pro died and the only
recovery tools I could find wanted hundreds of dollars, and I wasn't
totally convinced that they weren't shipping off the data to some intelligence
agency.

## License ##
    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
