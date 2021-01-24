// filesys.cc
//	Routines to manage the overall operation of the file system.
//	Implements routines to map from textual file names to files.
//
//	Each file in the file system has:
//	   A file header, stored in a sector on disk
//		(the size of the file header data structure is arranged
//		to be precisely the size of 1 disk sector)
//	   A number of data blocks
//	   An entry in the file system directory
//
// 	The file system consists of several data structures:
//	   A bitmap of free disk sectors (cf. bitmap.h)
//	   A directory of file names and file headers
//
//      Both the bitmap and the directory are represented as normal
//	files.  Their file headers are located in specific sectors
//	(sector 0 and sector 1), so that the file system can find them
//	on bootup.
//
//	The file system assumes that the bitmap and directory files are
//	kept "open" continuously while Nachos is running.
//
//	For those operations (such as Create, Remove) that modify the
//	directory and/or bitmap, if the operation succeeds, the changes
//	are written immediately back to disk (the two files are kept
//	open during all this time).  If the operation fails, and we have
//	modified part of the directory and/or bitmap, we simply discard
//	the changed version, without writing it back to disk.
//
// 	Our implementation at this point has the following restrictions:
//
//	   there is no synchronization for concurrent accesses
//	   files have a fixed size, set when the file is created
//	   files cannot be bigger than about 3KB in size
//	   there is no hierarchical directory structure, and only a limited
//	     number of files can be added to the system
//	   there is no attempt to make the system robust to failures
//	    (if Nachos exits in the middle of an operation that modifies
//	    the file system, it may corrupt the disk)
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation
// of liability and disclaimer of warranty provisions.
#ifndef FILESYS_STUB

#include "copyright.h"
#include "debug.h"
#include "disk.h"
#include "pbitmap.h"
#include "directory.h"
#include "filehdr.h"
#include "filesys.h"

// Sectors containing the file headers for the bitmap of free sectors,
// and the directory of files.  These file headers are placed in well-known
// sectors, so that they can be located on boot-up.
#define FreeMapSector 		0
#define DirectorySector 	1

// Initial file sizes for the bitmap and directory; until the file system
// supports extensible files, the directory size sets the maximum number
// of files that can be loaded onto the disk.
#define FreeMapFileSize 	(NumSectors / BitsInByte)
#define NumDirEntries 		64
#define DirectoryFileSize 	(sizeof(DirectoryEntry) * NumDirEntries)

//----------------------------------------------------------------------
// FileSystem::FileSystem
// 	Initialize the file system.  If format = TRUE, the disk has
//	nothing on it, and we need to initialize the disk to contain
//	an empty directory, and a bitmap of free sectors (with almost but
//	not all of the sectors marked as free).
//
//	If format = FALSE, we just have to open the files
//	representing the bitmap and the directory.
//
//	"format" -- should we initialize the disk?
//----------------------------------------------------------------------

FileSystem::FileSystem(bool format)
{
    DEBUG(dbgFile, "Initializing the file system.");
    if (format) {
        PersistentBitmap *freeMap = new PersistentBitmap(NumSectors);
        Directory *directory = new Directory(NumDirEntries);
		FileHeader *mapHdr = new FileHeader;
		FileHeader *dirHdr = new FileHeader;

        DEBUG(dbgFile, "Formatting the file system.");

		// First, allocate space for FileHeaders for the directory and bitmap
		// (make sure no one else grabs these!)
		freeMap->Mark(FreeMapSector);
		freeMap->Mark(DirectorySector);

		// Second, allocate space for the data blocks containing the contents
		// of the directory and bitmap files.  There better be enough space!

		ASSERT(mapHdr->Allocate(freeMap, FreeMapFileSize));
		ASSERT(dirHdr->Allocate(freeMap, DirectoryFileSize));

		// Flush the bitmap and directory FileHeaders back to disk
		// We need to do this before we can "Open" the file, since open
		// reads the file header off of disk (and currently the disk has garbage
		// on it!).

        DEBUG(dbgFile, "Writing headers back to disk.");
		mapHdr->WriteBack(FreeMapSector);
		dirHdr->WriteBack(DirectorySector);

		// OK to open the bitmap and directory files now
		// The file system operations assume these two files are left open
		// while Nachos is running.

        freeMapFile = new OpenFile(FreeMapSector);
        directoryFile = new OpenFile(DirectorySector);

		// Once we have the files "open", we can write the initial version
		// of each file back to disk.  The directory at this point is completely
		// empty; but the bitmap has been changed to reflect the fact that
		// sectors on the disk have been allocated for the file headers and
		// to hold the file data for the directory and bitmap.

        DEBUG(dbgFile, "Writing bitmap and directory back to disk.");
		freeMap->WriteBack(freeMapFile);	 // flush changes to disk
		directory->WriteBack(directoryFile);

		if (debug->IsEnabled('f')) {
			freeMap->Print();
			directory->Print();
        }
        delete freeMap;
		delete directory;
		delete mapHdr;
		delete dirHdr;
    } else {
		// if we are not formatting the disk, just open the files representing
		// the bitmap and directory; these are left open while Nachos is running
        freeMapFile = new OpenFile(FreeMapSector);
        directoryFile = new OpenFile(DirectorySector);
    }
}

//----------------------------------------------------------------------
// MP4 mod tag
// FileSystem::~FileSystem
//----------------------------------------------------------------------
FileSystem::~FileSystem()
{
	delete freeMapFile;
	delete directoryFile;
}

//----------------------------------------------------------------------
// FileSystem::Create
// 	Create a file in the Nachos file system (similar to UNIX create).
//	Since we can't increase the size of files dynamically, we have
//	to give Create the initial size of the file.
//
//	The steps to create a file are:
//	  Make sure the file doesn't already exist
//        Allocate a sector for the file header
// 	  Allocate space on disk for the data blocks for the file
//	  Add the name to the directory
//	  Store the new file header on disk
//	  Flush the changes to the bitmap and the directory back to disk
//
//	Return TRUE if everything goes ok, otherwise, return FALSE.
//
// 	Create fails if:
//   		file is already in directory
//	 	no free space for file header
//	 	no free entry for file in directory
//	 	no free space for data blocks for the file
//
// 	Note that this implementation assumes there is no concurrent access
//	to the file system!
//
//	"name" -- name of file to be created
//	"initialSize" -- size of file to be created
//----------------------------------------------------------------------

int
FileSystem::Create(char *name, int initialSize)
{
    Directory *directory = new Directory(NumDirEntries);
    PersistentBitmap *freeMap;
    FileHeader *hdr;
    int sector;
    bool success;

    DEBUG(dbgFile, "Creating file " << name << " size " << initialSize);

    directory->FetchFrom(directoryFile);

    char *tmpName = strtok(name, "/");
    OpenFile *directoryObj = directoryFile;
    while(tmpName != NULL){
        sector = directory->Find(tmpName);
        // cout << "Find " << tmpName << sector << endl;
        if(sector == -1)    break;
        // cout << "create in " << tmpName << ' ' << sector << endl;
        directoryObj = new OpenFile(sector);
        directory->FetchFrom(directoryObj);
        tmpName = strtok(NULL, "/");
    }

    // cout << "create file " << tmpName << endl;
    if (directory->Find(tmpName) != -1) //returns -1 of it's not in the directory
        success = FALSE;			// file is already in directory
    else {
        // cout << "sadjhlasd\n";
        freeMap = new PersistentBitmap(freeMapFile, NumSectors);
        // cout << "?????\n";
        sector = freeMap->FindAndSet();	// find a sector to hold the file header
        // cout << "!!!!!!!!!\n";
    	// bool isAdd = directory->Add(tmpName, sector, FALSE);
        if (sector == -1)
            success = FALSE;		// no free block for file header
        else if (!directory->Add(tmpName, sector, FALSE))
            success = FALSE;	// no space in directory
        else {
            // cout << "else\n";
            hdr = new FileHeader;
            if (!hdr->Allocate(freeMap, initialSize))
                success = FALSE;	// no space on disk for data
            else {
                success = TRUE;
            // everthing worked, flush all changes back to disk
                hdr->WriteBack(sector);
                directory->WriteBack(directoryObj);
                freeMap->WriteBack(freeMapFile);
            }
            delete hdr;
        }
        delete freeMap;
    }
    // cout << success << endl;
    delete directory;
    return success;
}

int
FileSystem::CreateAdirectory(char *name)
{
    Directory *directory = new Directory(NumDirEntries);
    Directory *subDirectory = new Directory(NumDirEntries);
    OpenFile* subDirectoryFile;
    PersistentBitmap *freeMap;
    FileHeader *hdr;
    int sector;
    bool success;


    DEBUG(dbgFile, "Creating a directory " << name);

    directory->FetchFrom(directoryFile);

    char *tmpName = strtok(name, "/");
    char *lastName = tmpName;
    OpenFile *directoryObj = directoryFile;
    char *Name;
    while(tmpName != NULL){
        sector = directory->Find(tmpName);
        if(sector == -1)    break;
        directoryObj = new OpenFile(sector);
        directory->FetchFrom(directoryObj);
        lastName = tmpName;
        tmpName = strtok(NULL, "/");
    }

    if(tmpName == NULL)
        Name = lastName;
    else Name = tmpName;
    
    if (directory->Find(Name) != -1)
      success = FALSE;			// file is already in directory
    else {
        freeMap = new PersistentBitmap(freeMapFile,NumSectors);
        sector = freeMap->FindAndSet();	// find a sector to hold the file header
    	// cout << ":" << NumSectors << endl;
        bool isAdd = directory->Add(Name, sector, TRUE);
        
        if (sector == -1 || !isAdd) // not a directory
            success = FALSE;
        else {
            hdr = new FileHeader;
            if (!hdr->Allocate(freeMap, DirectoryFileSize))
                success = FALSE;	// no space on disk for data
            else {
                success = TRUE;  // everthing worked, flush all changes back to disk
                hdr->WriteBack(sector);
                subDirectoryFile = new OpenFile(sector);
				subDirectory->WriteBack(subDirectoryFile);
                directory->WriteBack(directoryObj);
                freeMap->WriteBack(freeMapFile);
            }
            delete hdr;
        }
        delete freeMap;
    }
    delete subDirectoryFile;
	delete subDirectory;
    delete directory;
    return success;
}

//----------------------------------------------------------------------
// FileSystem::Open
// 	Open a file for reading and writing.
//	To open a file:
//	  Find the location of the file's header, using the directory
//	  Bring the header into memory
//
//	"name" -- the text name of the file to be opened
//----------------------------------------------------------------------

OpenFile *
FileSystem::Open(char *name)
{
    Directory *directory = new Directory(NumDirEntries);
    OpenFile *openFile = NULL;
    int sector;

    char *tmpName = strtok(name,"/");
    OpenFile *directoryObj = directoryFile;
    DEBUG(dbgFile, "Opening file" << name);

    while(tmpName!=NULL){
        directory->FetchFrom(directoryObj);
        sector = directory->Find(tmpName);
        if(sector == -1)
            break;
        directoryObj = new OpenFile(sector);
        tmpName = strtok(NULL,"/");
    }
    
    delete directory;
    return directoryObj; // return NULL if not found
}

//  The OpenAFile function is used for kernel open system call
OpenFileId FileSystem::OpenAFile(char *name) {
    Directory *directory = new Directory(NumDirEntries);
    OpenFile *openFile = NULL;
    int sector;

    char *tmpName = strtok(name, "/");
    char *lastName = tmpName;
    OpenFile *directoryObj = directoryFile;

    while(tmpName != NULL){
        sector = directory->Find(tmpName);
        if(sector == -1) break;
        directoryObj = new OpenFile(sector);
        directory->FetchFrom(directoryObj);
        lastName = tmpName;
        tmpName = strtok(NULL, "/");
    }

    DEBUG(dbgFile, "Opening A file" << name);
    directory->FetchFrom(directoryFile);
    sector = directory->Find(tmpName);
    if(sector >= 0){
        fileDescriptor = new OpenFile(sector);
        delete directory;
        return sector;
    }
    delete directory;
    return -1;
}

int FileSystem::WriteFile(char *buffer, int size, OpenFileId id){
    if (size >= 0 && fileDescriptor != NULL){
        int num = fileDescriptor->Write(buffer, size);
        return num;
    } else return -1;
}

int FileSystem::ReadFile(char *buffer, int size, OpenFileId id){
    if (size >= 0 && fileDescriptor != NULL){
        int num = fileDescriptor->Read(buffer, size);
        return num;
    } else return -1;
}

int FileSystem::CloseFile(OpenFileId id){
    if (fileDescriptor != NULL){
        delete fileDescriptor;
        fileDescriptor = NULL;
        return 1;
    } else return -1;
}
//----------------------------------------------------------------------
// FileSystem::Remove
// 	Delete a file from the file system.  This requires:
//	    Remove it from the directory
//	    Delete the space for its header
//	    Delete the space for its data blocks
//	    Write changes to directory, bitmap back to disk
//
//	Return TRUE if the file was deleted, FALSE if the file wasn't
//	in the file system.
//
//	"name" -- the text name of the file to be removed
//----------------------------------------------------------------------

bool
FileSystem::Remove(char *name)
{
    Directory *directory = new Directory(NumDirEntries);
    PersistentBitmap *freeMap;
    FileHeader *fileHdr;
    int sector;

    directory->FetchFrom(directoryFile);

    OpenFile *directoryObj = directoryFile;
    OpenFile *prevdirectoryObj = directoryFile;
    // cout << "ori name " << name << endl;
    char *tmpName = strtok(name, "/");
    char *lastName = tmpName;
    bool isFile = FALSE;
    
    while(tmpName != NULL){
        sector = directory->Find(tmpName);
        if(sector == -1 || !directory->isDirectory(tmpName)){
            isFile = TRUE;
            break;
        }
        prevdirectoryObj = directoryObj;
        directoryObj = new OpenFile(sector);
        directory->FetchFrom(directoryObj);
        lastName = tmpName;
        tmpName = strtok(NULL, "/");
    }

    if (tmpName == NULL){
        tmpName = lastName;
        // cout << "NULL?\n";
    }

    // cout << "normal remove\n";
    // cout << "tmp Name " << tmpName << endl;
    // cout << "nname " << lastName << endl;
    if (sector == -1) {
       return FALSE;			 // file not found
    }

    if (isFile) {
        // cout << tmpName << " is a file\n";
        directory->FetchFrom(directoryObj);
    } else {
        // cout << tmpName << " is a dir\n";
        directory->FetchFrom(prevdirectoryObj);
    }
    sector = directory->Find(tmpName);
    fileHdr = new FileHeader;
    fileHdr->FetchFrom(sector);
    freeMap = new PersistentBitmap(freeMapFile,NumSectors);
    fileHdr->Deallocate(freeMap);  		// remove data blocks
    freeMap->Clear(sector);			// remove header block
    directory->Remove(tmpName);
    freeMap->WriteBack(freeMapFile);		// flush to disk
    if(isFile)
        directory->WriteBack(directoryObj);        // flush to disk
    else
        directory->WriteBack(prevdirectoryObj);
    // cout << "normal remove success\n";
    delete fileHdr;
    delete directory;
    delete freeMap;
    // cout << "normal remove success1\n";
    return TRUE;
}

bool
FileSystem::RecursiveRemove(char *name)
{
    Directory *directory = new Directory(NumDirEntries);
	PersistentBitmap *freeMap;
	FileHeader *fileHdr;
    directory->FetchFrom(directoryFile);
    int sector, currentSector;
    OpenFile *directoryObj = directoryFile;
    char current[255], buffer[255];
    strcpy(current, name);
    // cout << "ori name " << name << endl;
    char *tmpName = strtok(name, "/");
    char *lastName = tmpName;
    while (tmpName!=NULL){
        sector = directory->Find(tmpName);
        if (sector == -1 || !directory->isDirectory(tmpName)){
            break;
        }
        directoryObj = new OpenFile(sector);
        directory->FetchFrom(directoryObj);
        lastName = tmpName;
        tmpName = strtok(NULL, "/");
    }

    currentSector = sector;
    if(tmpName==NULL){
        tmpName = lastName;
        // cout << "rNULL?\n";
    }

    // cout << "rr now\n";
    // cout << "tmpName is " << tmpName << endl;
    // cout << "lastName is " << lastName << endl; 

    DirectoryEntry *directoryTable = directory->table;
    if(directory->isDirectory(tmpName)){
        for(int i=0;i<directory->tableSize;i++){
            if(directoryTable[i].inUse){
                if(directoryTable[i].isDirectory){
                    int x = sprintf(buffer,"%s/%s", current, directoryTable[i].name);
                    RecursiveRemove(buffer);
                }else{
                    int x = sprintf(buffer,"%s/%s", current, directoryTable[i].name);
                    Remove(buffer);
                }
            }
        } 
        Remove(current);
    }else{
        Remove(current);
    }
    return TRUE;
}

//----------------------------------------------------------------------
// FileSystem::List
// 	List all the files in the file system directory.
//----------------------------------------------------------------------

void
FileSystem::List(char *name)
{
    Directory *directory = new Directory(NumDirEntries);
    int sector;
    directory->FetchFrom(directoryFile);
    char *tmpName = strtok(name, "/");
    OpenFile *directoryObj = directoryFile;
    // cout << name << endl;
    while(tmpName != NULL){
        sector = directory->Find(tmpName);
        // cout << sector << endl;
        // cout << "name:" << tmpName << endl;
        if(sector == -1)    break;
        directoryObj = new OpenFile(sector);
        directory->FetchFrom(directoryObj);
        tmpName = strtok(NULL, "/");
    }
    directory->List();
    delete directory;
    delete tmpName;
    delete directoryObj;
}

void
FileSystem::RecursiveList(char *name)
{
    Directory *directory = new Directory(NumDirEntries);
    int sector;
    char *tmpName = strtok(name, "/");
    OpenFile *directoryObj = directoryFile;
    while(tmpName != NULL){
        directory->FetchFrom(directoryObj);
        sector = directory->Find(tmpName);
        if(sector == -1)    break;
        directoryObj = new OpenFile(sector);
        tmpName = strtok(NULL, "/");
    }
    directory->FetchFrom(directoryObj);
    directory->RecursiveList();
    if(tmpName != NULL){
        delete directory;
        delete tmpName;
        delete directoryObj;
    }
}

//----------------------------------------------------------------------
// FileSystem::Print
// 	Print everything about the file system:
//	  the contents of the bitmap
//	  the contents of the directory
//	  for each file in the directory,
//	      the contents of the file header
//	      the data in the file
//----------------------------------------------------------------------

void
FileSystem::Print()
{
    FileHeader *bitHdr = new FileHeader;
    FileHeader *dirHdr = new FileHeader;
    PersistentBitmap *freeMap = new PersistentBitmap(freeMapFile,NumSectors);
    Directory *directory = new Directory(NumDirEntries);

    printf("Bit map file header:\n");
    bitHdr->FetchFrom(FreeMapSector);
    bitHdr->Print();

    printf("Directory file header:\n");
    dirHdr->FetchFrom(DirectorySector);
    dirHdr->Print();

    freeMap->Print();

    directory->FetchFrom(directoryFile);
    directory->Print();

    delete bitHdr;
    delete dirHdr;
    delete freeMap;
    delete directory;
}

#endif // FILESYS_STUB
