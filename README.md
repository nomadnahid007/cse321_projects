# cse321_projects
This repository contains two projects I did as a part of my Operating Systems course, CSE321, in undergraduate studies. My teammates MD Shahadat Hossain Shamim, Aowfi Adon Foraejy, and I had to complete the following two projects in the entire semester: 

#Project 1: 
In this project, we had to implement a UNIX shell using C. Our shell is able to perform basic linux commands, I/O redirections and handle errors.  We had to cover the following specifications:
1. Display a command prompt (e.g., sh> ) and read user input. 
2. Parse and execute system commands. 
For example, running “pwd” will output the absolute path of the directory that your shell is 
working on. 
3. Support input (<) and output (> and >>) redirection.  
4. Support command piping (|). Your shell should support any number of piping. 
For example, “command1 | command2 | command3 | command4” should work 
5. Support multiple commands in-line separated by semicolon (;) 
6. Support multiple command in sequence using (&&) 
7. Support history of executed commands 
8. Support signal handling. Pressing CTRL+C should terminate the currently running 
command inside your shell, not your shell.

#Project 2:
In this project, we had design and implement a file system consistency checker, vsfsck, for a 
custom virtual file system (VSFS). Our tool had to be responsible for verifying the integrity and 
consistency of essential file system structures, including: 
● Superblock 
● Inodes 
● Data blocks 
● Inode and data bitmaps 
The checker will operate on a file system image (vsfs.img), identifying and reporting any 
inconsistencies found. 

We were provided with a corrupted file system image (vsfs.img) containing various errors. 
Our objectives were to: 
1. Analyze the file system image using your vsfsck tool. 
2. Identify all inconsistencies and structural issues. 
3. Fix the detected errors to restore the file system’s integrity. 
4. Ensure that the corrected file system image is error-free when re-checked with your tool 

Features 
1. Superblock Validator 
Verifies: 
a. Magic number (must be 0xd34d) 
b. Block size (must be 4096) 
c. Total number of blocks (must be 64) 
d. Validity of key block pointers: inode bitmap, data bitmap, inode table start, data 
block start 
e. Inode size (256) and count constraints 
2. Data Bitmap Consistency Checker 
Verifies: 
a. Every block marked used in the data bitmap is actually referenced by a valid 
inode 
b. Every block referenced by an inode is marked as used in the data bitmap 
3. Inode Bitmap Consistency Checker 
Verifies: 
a. Each bit set in the inode bitmap corresponds to a valid inode 
(Hint: An inode is valid if its number of link is greater than 0 and delete time is set 
to 0) 
b. Conversely, every such inode is marked as used in the bitmap 
4. Duplicate Checker detects blocks referenced by multiple inodes 
5. Bad block checker detects blocks with indices outside valid range 
