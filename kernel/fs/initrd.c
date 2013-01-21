#include <types.h>
#include <string.h>
#include "hal/hal.h"
#include "mm/malloc.h"
#include "dirent.h"
#include "initrd.h"
#include "debug.h"

struct ramfs_node {
	char name[128];
	uint32_t type;
	uint32_t inode;
	uint32_t length;
	uint32_t mask;
	uint8_t *data;
};

extern struct vfs_node *vfs_node_alloc(struct vfs_mount *mnt, uint32_t type,
				       struct vfs_node_ops *ops, void *data);

static int initrd_mount(struct vfs_mount *mnt, int flags, const void *data);
static int initrd_read_node(struct vfs_mount *mnt, ino_t id, struct vfs_node **np);

struct vfs_type _ramfs_type = {
	.name = "ramfs",
	.desc = "Ramdisk file system",
	.ref_count = 0,
	.mount = initrd_mount,
};

static struct initrd_header *initrd_hdr = NULL;
static struct initrd_file_header *file_hdrs = NULL;
static struct ramfs_node *_initrd_nodes = NULL;

static int _nr_initrd_nodes = 0;
static int _nr_total_initrd_nodes = 0;

static int initrd_create(struct vfs_node *parent, const char *name,
			 uint32_t type, struct vfs_node **np)
{
	int rc = -1, pos;
	struct vfs_node *n = NULL;

	ASSERT(parent->type == VFS_DIRECTORY);
	
	DEBUG(DL_DBG, ("create(%s), type(%d).\n", name, type));

	if (!np || type != VFS_DIRECTORY) {	// Only support create directory now
		goto out;
	}

	if (_nr_initrd_nodes >= _nr_total_initrd_nodes) {
		goto out;
	}

	n = vfs_node_alloc(parent->mount, VFS_DIRECTORY, parent->ops, NULL);
	if (!n) {
		goto out;
	}

	/* Get the position we should add the new node to */
	pos = _nr_initrd_nodes;
	
	strncpy(_initrd_nodes[pos].name, name, 127);
	_initrd_nodes[pos].inode = _nr_initrd_nodes + 1;
	_initrd_nodes[pos].type = type;
	_initrd_nodes[pos].length = 0;
	_nr_initrd_nodes++;

	/* Initialize the vfs_node which we created */
	strncpy(n->name, name, 127);
	n->ino = _initrd_nodes[pos].inode;
	n->length = _initrd_nodes[pos].length;

	*np = n;

	rc = 0;

 out:
	return rc;
}

static int initrd_close(struct vfs_node *node)
{
	int rc = 0;

	DEBUG(DL_DBG, ("close(%s:%d) ref_count(%d).\n",
		       node->name, node->ino, node->ref_count));

	return rc;
}

static int initrd_read(struct vfs_node *node, uint32_t offset,
		       uint32_t size, uint8_t *buffer)
{
	int i;

	/* Find the coresponding initrd node */
	for (i = 0; i < _nr_initrd_nodes; i++) {
		if (_initrd_nodes[i].inode == node->ino) {
			break;
		}
	}
	
	if (offset > _initrd_nodes[i].length) {
		DEBUG(DL_DBG, ("offset(%d), length(%d)\n",
			       offset, _initrd_nodes[i].length));
		return 0;
	}
	
	if (offset + size > _initrd_nodes[i].length) {
		size = _initrd_nodes[i].length - offset;
	}
	
	memcpy(buffer, _initrd_nodes[i].data + offset, size);

	return size;
}

/*
 * Caller should free the returned dirent by kfree
 */
static int initrd_readdir(struct vfs_node *node, uint32_t index, struct dirent **dentry)
{
	int rc = -1, pos;
	struct dirent *new_dentry = NULL;

	ASSERT(dentry != NULL);
	
	if (index >= _nr_initrd_nodes) {
		goto out;
	}

	/* Get the position from the index, currently we just use index */
	pos = index;

	new_dentry = kmalloc(sizeof(struct dirent), 0);
	if (!new_dentry) {
		DEBUG(DL_INF, ("allocate dirent failed, node(%s), index(%d).\n",
			       node->name, index));
		goto out;
	}

	memset(new_dentry, 0, sizeof(struct dirent));
	strncpy(new_dentry->d_name, _initrd_nodes[pos].name, 128);
	new_dentry->d_ino = _initrd_nodes[pos].inode;
	*dentry = new_dentry;
	rc = 0;

 out:
	return rc;
}

static int initrd_finddir(struct vfs_node *node, const char *name, ino_t *id)
{
	int rc = -1, i;
	
	for (i = 0; i < _nr_initrd_nodes; i++) {
		if (strcmp(name, _initrd_nodes[i].name) == 0) {
			*id = _initrd_nodes[i].inode;
			rc = 0;
			break;
		}
	}

	if (rc != 0) {
		DEBUG(DL_DBG, ("node(%s), name(%s), not found.\n",
			       node->name, name));
	}

	return rc;
}

static struct vfs_node_ops _ramfs_node_ops = {
	.read = initrd_read,
	.write = NULL,
	.create = initrd_create,
	.close = initrd_close,
	.readdir = initrd_readdir,
	.finddir = initrd_finddir,
};

static int initrd_read_node(struct vfs_mount *mnt, ino_t id, struct vfs_node **np)
{
	int rc = -1, i;
	struct vfs_node *node;

	ASSERT(np != NULL);
	
	for (i = 0; i < _nr_initrd_nodes; i++) {
		if (_initrd_nodes[i].inode == id) {
			node = vfs_node_alloc(mnt, _initrd_nodes[i].type,
					      &_ramfs_node_ops, NULL);
			if (!node) {
				break;
			}
			
			node->ino = id;
			node->length = _initrd_nodes[i].length;
			node->mask = _initrd_nodes[i].mask;
			strncpy(node->name, _initrd_nodes[i].name, 128);
			
			*np = node;
			rc = 0;
			break;
		}
	}

	return rc;
}

void init_initrd(uint32_t location)
{
	int i;
	size_t size;
	
	initrd_hdr = (struct initrd_header *)location;
	file_hdrs = (struct initrd_file_header *)(location + sizeof(struct initrd_header));

	/* Initialize the file nodes in root directory, we will allocate 12 more
	 * nodes for creating new nodes.
	 */
	_nr_total_initrd_nodes = initrd_hdr->nr_files + 12;
	_nr_initrd_nodes = initrd_hdr->nr_files;

	size = sizeof(struct ramfs_node) * _nr_total_initrd_nodes;
	_initrd_nodes = (struct ramfs_node *)kmalloc(size, 0);
	ASSERT(_initrd_nodes != NULL);
	
	memset(_initrd_nodes, 0, size);
	
	/* For each file in the root directory initialize the corresponding ramfs
	 * nodes.
	 */
	for (i = 0; i < _nr_initrd_nodes; i++) {
		file_hdrs[i].offset += location;

		/* Create a new file node */
		strcpy(_initrd_nodes[i].name, file_hdrs[i].name);
		_initrd_nodes[i].inode = i + 1;
		_initrd_nodes[i].type = VFS_FILE;
		_initrd_nodes[i].length = file_hdrs[i].length;
		_initrd_nodes[i].mask = 0755;
		_initrd_nodes[i].data = file_hdrs[i].offset;
	}
}

static struct vfs_mount_ops _ramfs_mount_ops = {
	.umount = NULL,
	.flush = NULL,
	.read_node = initrd_read_node,
};

int initrd_mount(struct vfs_mount *mnt, int flags, const void *data)
{
	int rc = -1;

	mnt->ops = &_ramfs_mount_ops;
	mnt->root = vfs_node_alloc(mnt, VFS_DIRECTORY, &_ramfs_node_ops, NULL);
	strncpy(mnt->root->name, "initrd-root", 128);
	ASSERT(mnt->root != NULL);

	init_initrd((uint32_t)data);
	
	rc = 0;

	return rc;
}

int initrd_init(void)
{
	int rc;
	
	rc = vfs_type_register(&_ramfs_type);
	if (rc != 0) {
		DEBUG(DL_DBG, ("module initrd initialize failed.\n"));
	} else {
		DEBUG(DL_DBG, ("module initrd initialize successfully.\n"));
	}

	return rc;
}
