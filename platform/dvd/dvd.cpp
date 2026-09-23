// DVD: the disc's file system is served from an extracted disc directory
// (<root>/files, with the disc's own FST at <root>/../sys/fst.bin so entry
// numbers match the real disc). Reads are synchronous host preads; async
// completions are delivered as interrupts at the next check point.
#include "port_compat.h"
#include "port_os.h"
#include "port_platform.h"
#include <dolphin/dvd.h>
#include <dolphin/os.h>
#include <string>
#include <vector>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <strings.h>
#include <algorithm>

namespace {

struct Entry {
	bool dir;
	std::string name;
	std::string host; // host path
	u32 parent;       // dirs: parent entry
	u32 next;         // dirs: one past last descendant
	u32 length;       // files: size
	int fd;
};

std::vector<Entry> g_fst;
u32 g_cwd;
DVDDiskID g_disk_id;

u32 be32(const u8* p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }

bool load_fst_bin(const std::string& files)
{
	std::string sys = files + "/../sys/fst.bin";
	FILE* f         = fopen(sys.c_str(), "rb");
	if (!f)
		return false;
	std::vector<u8> b;
	u8 buf[4096];
	size_t n;
	while ((n = fread(buf, 1, sizeof buf, f)) > 0)
		b.insert(b.end(), buf, buf + n);
	fclose(f);
	if (b.size() < 12)
		return false;
	u32 count = be32(&b[8]);
	if (count * 12 > b.size())
		return false;
	const char* strings = (const char*)&b[count * 12];
	g_fst.resize(count);
	for (u32 i = 0; i < count; i++) {
		const u8* e = &b[i * 12];
		Entry& d    = g_fst[i];
		d.dir       = e[0] != 0;
		d.name      = i ? std::string(strings + (be32(e) & 0xFFFFFF)) : std::string();
		d.parent    = d.dir ? be32(e + 4) : 0;
		d.next      = d.dir ? be32(e + 8) : 0;
		d.length    = d.dir ? 0 : be32(e + 8);
		d.fd        = -1;
	}
	// Host paths: walk directories in order, tracking the path stack.
	g_fst[0].host = files;
	std::vector<u32> stack(1, 0);
	for (u32 i = 1; i < count; i++) {
		while (stack.size() > 1 && i >= g_fst[stack.back()].next)
			stack.pop_back();
		Entry& d = g_fst[i];
		d.host   = g_fst[stack.back()].host + "/" + d.name;
		if (d.dir)
			stack.push_back(i);
	}
	return true;
}

// Fallback without sys/fst.bin: build an FST from the directory tree, sorted
// case-insensitively like the mastering tools do.
void walk(const std::string& host, u32 parent)
{
	DIR* d = opendir(host.c_str());
	if (!d)
		return;
	std::vector<std::string> names;
	while (dirent* e = readdir(d))
		if (strcmp(e->d_name, ".") && strcmp(e->d_name, ".."))
			names.push_back(e->d_name);
	closedir(d);
	std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) {
		return strcasecmp(a.c_str(), b.c_str()) < 0;
	});
	for (size_t i = 0; i < names.size(); i++) {
		Entry e;
		e.name = names[i];
		e.host = host + "/" + names[i];
		e.fd   = -1;
		struct stat st;
		stat(e.host.c_str(), &st);
		e.dir    = S_ISDIR(st.st_mode);
		e.parent = parent;
		e.length = e.dir ? 0 : (u32)st.st_size;
		e.next   = 0;
		u32 idx  = (u32)g_fst.size();
		g_fst.push_back(e);
		if (e.dir) {
			walk(e.host, idx);
			g_fst[idx].next = (u32)g_fst.size();
		}
	}
}

s32 lookup(const char* path)
{
	u32 dir = g_cwd;
	if (*path == '/') {
		dir = 0;
		while (*path == '/')
			path++;
	}
	while (*path) {
		const char* end = strchr(path, '/');
		size_t len      = end ? (size_t)(end - path) : strlen(path);
		if (len == 0) {
		} else if (len == 1 && path[0] == '.') {
		} else if (len == 2 && path[0] == '.' && path[1] == '.') {
			dir = g_fst[dir].parent;
		} else {
			s32 found = -1;
			for (u32 i = dir + 1; i < g_fst[dir].next;) {
				const Entry& e = g_fst[i];
				if (e.name.size() == len && strncasecmp(e.name.c_str(), path, len) == 0) {
					found = (s32)i;
					break;
				}
				i = e.dir ? e.next : i + 1;
			}
			if (found < 0)
				return -1;
			if (!end)
				return found;
			if (!g_fst[found].dir)
				return -1;
			dir = (u32)found;
		}
		if (!end)
			break;
		path = end + 1;
	}
	return (s32)dir;
}

int file_fd(u32 entry)
{
	Entry& e = g_fst[entry];
	if (e.fd < 0)
		e.fd = open(e.host.c_str(), O_RDONLY);
	return e.fd;
}

s32 do_read(DVDFileInfo* fi, void* addr, s32 length, s32 offset)
{
	u32 entry = fi->startAddr;
	if (entry >= g_fst.size() || g_fst[entry].dir)
		return DVD_RESULT_FATAL_ERROR;
	int fd = file_fd(entry);
	if (fd < 0)
		return DVD_RESULT_FATAL_ERROR;
	s32 done = 0;
	while (done < length) {
		ssize_t r = pread(fd, (u8*)addr + done, length - done, offset + done);
		if (r <= 0)
			break;
		done += (s32)r;
	}
	fi->cb.transferredSize = done;
	return done;
}

} // namespace

extern "C" void port_dvd_init(void)
{
	std::string root = port_disc_root;
	if (!load_fst_bin(root)) {
		port_log("[dvd] no sys/fst.bin next to %s; building the FST from the directory tree\n", root.c_str());
		Entry r;
		r.dir    = true;
		r.host   = root;
		r.parent = 0;
		r.length = 0;
		r.fd     = -1;
		g_fst.push_back(r);
		walk(root, 0);
		g_fst[0].next = (u32)g_fst.size();
	}
	g_cwd = 0;
	memset(&g_disk_id, 0, sizeof g_disk_id);
	FILE* f = fopen((root + "/../sys/boot.bin").c_str(), "rb");
	if (f) {
		fread(&g_disk_id, 1, sizeof g_disk_id, f);
		fclose(f);
	} else {
		memcpy(g_disk_id.gameName, "GMSE", 4);
		memcpy(g_disk_id.company, "01", 2);
	}
	port_log("[dvd] FST: %u entries from %s\n", (unsigned)g_fst.size(), root.c_str());
}

extern "C" void DVDInit(void) {}
extern "C" s32 DVDConvertPathToEntrynum(char* path)
{
	s32 r = lookup(path);
	if (r < 0)
		port_log("[dvd] path not found: %s\n", path);
	return r;
}

extern "C" BOOL DVDFastOpen(s32 entry, DVDFileInfo* fi)
{
	if (entry < 0 || (u32)entry >= g_fst.size() || g_fst[entry].dir)
		return FALSE;
	memset(fi, 0, sizeof *fi);
	fi->startAddr = (u32)entry;
	fi->length    = g_fst[entry].length;
	fi->cb.state  = DVD_STATE_END;
	return TRUE;
}

extern "C" BOOL DVDOpen(char* name, DVDFileInfo* fi)
{
	s32 e = DVDConvertPathToEntrynum(name);
	if (e < 0)
		return FALSE;
	return DVDFastOpen(e, fi);
}

extern "C" BOOL DVDClose(DVDFileInfo* fi) { return TRUE; }

extern "C" BOOL DVDChangeDir(char* dirName)
{
	s32 e = lookup(dirName);
	if (e < 0 || !g_fst[e].dir)
		return FALSE;
	g_cwd = (u32)e;
	return TRUE;
}

extern "C" BOOL DVDGetCurrentDir(char* path, u32 maxlen)
{
	std::string p;
	for (u32 d = g_cwd; d != 0; d = g_fst[d].parent)
		p = "/" + g_fst[d].name + p;
	if (p.empty())
		p = "/";
	strncpy(path, p.c_str(), maxlen);
	return TRUE;
}

extern "C" s32 DVDReadPrio(DVDFileInfo* fi, void* addr, s32 length, s32 offset, s32 prio)
{
	fi->cb.state = DVD_STATE_BUSY;
	s32 r        = do_read(fi, addr, length, offset);
	fi->cb.state = r < 0 ? DVD_STATE_FATAL_ERROR : DVD_STATE_END;
	port_irq_check();
	return r;
}

extern "C" BOOL DVDReadAsyncPrio(DVDFileInfo* fi, void* addr, s32 length, s32 offset, DVDCallback cb, s32 prio)
{
	fi->cb.state = DVD_STATE_BUSY;
	fi->callback = cb;
	s32 r        = do_read(fi, addr, length, offset);
	port_irq_defer([fi, r, cb]() {
		fi->cb.state = r < 0 ? DVD_STATE_FATAL_ERROR : DVD_STATE_END;
		if (cb)
			cb(r, fi);
	});
	port_irq_kick();
	return TRUE;
}

extern "C" s32 DVDGetCommandBlockStatus(DVDCommandBlock* block)
{
	port_irq_check();
	return block->state;
}
extern "C" s32 DVDGetFileInfoStatus(DVDFileInfo* fi) { return DVDGetCommandBlockStatus(&fi->cb); }
extern "C" s32 DVDGetDriveStatus(void)
{
	port_irq_check();
	return DVD_STATE_END;
}
extern "C" BOOL DVDCheckDisk(void) { return TRUE; }
extern "C" DVDDiskID* DVDGetCurrentDiskID(void) { return &g_disk_id; }
extern "C" s32 DVDGetTransferredSize(DVDFileInfo* fi) { return fi->cb.transferredSize; }
extern "C" s32 DVDCancel(DVDCommandBlock* block)
{
	block->state = DVD_STATE_END;
	return 0;
}
extern "C" BOOL DVDCancelAsync(DVDCommandBlock* block, DVDCBCallback cb)
{
	block->state = DVD_STATE_END;
	if (cb)
		port_irq_defer([block, cb]() { cb(0, block); });
	return TRUE;
}

extern "C" BOOL DVDOpenDir(char* dirName, DVDDir* dir)
{
	s32 e = lookup(dirName);
	if (e < 0 || !g_fst[e].dir)
		return FALSE;
	dir->entryNum = (u32)e;
	dir->location = (u32)e + 1;
	dir->next     = g_fst[e].next;
	return TRUE;
}

extern "C" BOOL DVDReadDir(DVDDir* dir, DVDDirEntry* ent)
{
	u32 i = dir->location;
	if (i <= dir->entryNum || i >= dir->next)
		return FALSE;
	ent->entryNum = i;
	ent->isDir    = g_fst[i].dir;
	ent->name     = (char*)g_fst[i].name.c_str();
	dir->location = g_fst[i].dir ? g_fst[i].next : i + 1;
	return TRUE;
}

extern "C" BOOL DVDCloseDir(DVDDir* dir) { return TRUE; }

// Streaming audio (DVD ADPCM streams) is not emulated: report success with no data.
extern "C" BOOL DVDPrepareStreamAsync(DVDFileInfo* fi, u32 length, u32 offset, DVDCallback cb)
{
	if (cb)
		port_irq_defer([fi, cb]() { cb(0, fi); });
	return TRUE;
}
extern "C" BOOL DVDCancelStreamAsync(DVDCommandBlock* block, DVDCBCallback cb)
{
	if (cb)
		port_irq_defer([block, cb]() { cb(0, block); });
	return TRUE;
}
extern "C" BOOL DVDStopStreamAtEndAsync(DVDCommandBlock* block, DVDCBCallback cb)
{
	if (cb)
		port_irq_defer([block, cb]() { cb(0, block); });
	return TRUE;
}
extern "C" BOOL DVDGetStreamPlayAddrAsync(DVDCommandBlock* block, DVDCBCallback cb)
{
	if (cb)
		port_irq_defer([block, cb]() { cb(0, block); });
	return TRUE;
}
