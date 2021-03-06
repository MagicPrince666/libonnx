#include <hmap.h>

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(array) \
	(sizeof(array) / sizeof((array)[0]))
#endif

struct hmap_t * hmap_alloc(unsigned int size)
{
	struct hmap_t * m;
	int i;

	if(size < 16)
		size = 16;
	if(size & (size - 1))
		size = roundup_pow_of_two(size);

	m = malloc(sizeof(struct hmap_t));
	if(!m)
		return NULL;

	m->hash = malloc(sizeof(struct hlist_head) * size);
	if(!m->hash)
	{
		free(m);
		return NULL;
	}
	for(i = 0; i < size; i++)
		init_hlist_head(&m->hash[i]);
	init_list_head(&m->list);
	m->size = size;
	m->n = 0;

	return m;
}

void hmap_free(struct hmap_t * m, void (*cb)(struct hmap_entry_t *))
{
	if(m)
	{
		hmap_clear(m, cb);
		free(m->hash);
		free(m);
	}
}

void hmap_clear(struct hmap_t * m, void (*cb)(struct hmap_entry_t *))
{
	struct hmap_entry_t * pos, * n;

	if(m)
	{
		list_for_each_entry_safe(pos, n, &m->list, head)
		{
			hlist_del(&pos->node);
			list_del(&pos->head);
			m->n--;
			if(cb)
				cb(pos);
			free(pos->key);
			free(pos);
		}
	}
}

static void hmap_resize(struct hmap_t * m, unsigned int size)
{
	struct hmap_entry_t * pos, * n;
	struct hlist_head * hash;
	int i;

	if(!m)
		return;

	if(size < 16)
		size = 16;
	if(size & (size - 1))
		size = roundup_pow_of_two(size);

	hash = malloc(sizeof(struct hlist_head) * size);
	if(!hash)
		return;
	for(i = 0; i < size; i++)
		init_hlist_head(&hash[i]);

	list_for_each_entry_safe(pos, n, &m->list, head)
	{
		hlist_del(&pos->node);
	}
	free(m->hash);

	m->hash = hash;
	m->size = size;
	list_for_each_entry_safe(pos, n, &m->list, head)
	{
		hlist_add_head(&pos->node, &m->hash[shash(pos->key) & (m->size - 1)]);
	}
}

void hmap_add(struct hmap_t * m, const char * key, void * value)
{
	struct hmap_entry_t * pos;
	struct hlist_node * n;

	if(!m || !key)
		return;

	hlist_for_each_entry_safe(pos, n, &m->hash[shash(key) & (m->size - 1)], node)
	{
		if(strcmp(pos->key, key) == 0)
		{
			if(pos->value != value)
				pos->value = value;
			return;
		}
	}

	if(m->n > (m->size >> 1))
		hmap_resize(m, m->size << 1);

	pos = malloc(sizeof(struct hmap_entry_t));
	if(!pos)
		return;

	pos->key = strdup(key);
	pos->value = value;
	init_hlist_node(&pos->node);
	hlist_add_head(&pos->node, &m->hash[shash(pos->key) & (m->size - 1)]);
	init_list_head(&pos->head);
	list_add_tail(&pos->head, &m->list);
	m->n++;
}

void hmap_remove(struct hmap_t * m, const char * key)
{
	struct hmap_entry_t * pos;
	struct hlist_node * n;

	if(!m || !key)
		return;

	if((m->size > 16) && (m->n < (m->size >> 1)))
		hmap_resize(m, m->size >> 1);

	hlist_for_each_entry_safe(pos, n, &m->hash[shash(key) & (m->size - 1)], node)
	{
		if(strcmp(pos->key, key) == 0)
		{
			hlist_del(&pos->node);
			list_del(&pos->head);
			m->n--;
			free(pos->key);
			free(pos);
			return;
		}
	}
}

static struct list_head * merge(void * priv, int (*cmp)(void * priv, struct list_head * a, struct list_head * b), struct list_head * a, struct list_head * b)
{
	struct list_head head, * tail = &head;

	while(a && b)
	{
		if((*cmp)(priv, a, b) <= 0)
		{
			tail->next = a;
			a = a->next;
		}
		else
		{
			tail->next = b;
			b = b->next;
		}
		tail = tail->next;
	}
	tail->next = a ? a : b;
	return head.next;
}

static void merge_and_restore_back_links(void * priv, int (*cmp)(void * priv, struct list_head * a, struct list_head * b), struct list_head * head, struct list_head * a, struct list_head * b)
{
	struct list_head * tail = head;
	unsigned char count = 0;

	while(a && b)
	{
		if((*cmp)(priv, a, b) <= 0)
		{
			tail->next = a;
			a->prev = tail;
			a = a->next;
		}
		else
		{
			tail->next = b;
			b->prev = tail;
			b = b->next;
		}
		tail = tail->next;
	}
	tail->next = a ? a : b;

	do {
		if(!(++count))
			(*cmp)(priv, tail->next, tail->next);
		tail->next->prev = tail;
		tail = tail->next;
	} while(tail->next);

	tail->next = head;
	head->prev = tail;
}

static void lsort(void * priv, struct list_head * head, int (*cmp)(void * priv, struct list_head * a, struct list_head * b))
{
	struct list_head * part[20 + 1];
	struct list_head * list;
	int maxlev = 0;
	int lev;

	if(list_empty(head))
		return;

	memset(part, 0, sizeof(part));
	head->prev->next = NULL;
	list = head->next;

	while(list)
	{
		struct list_head * cur = list;
		list = list->next;
		cur->next = NULL;

		for(lev = 0; part[lev]; lev++)
		{
			cur = merge(priv, cmp, part[lev], cur);
			part[lev] = NULL;
		}
		if(lev > maxlev)
		{
			if(lev >= ARRAY_SIZE(part) - 1)
				lev--;
			maxlev = lev;
		}
		part[lev] = cur;
	}
	for(lev = 0; lev < maxlev; lev++)
	{
		if(part[lev])
			list = merge(priv, cmp, part[lev], list);
	}
	merge_and_restore_back_links(priv, cmp, head, part[maxlev], list);
}

static int hmap_compare(void * priv, struct list_head * a, struct list_head * b)
{
	char * keya = (char *)list_entry(a, struct hmap_entry_t, head)->key;
	char * keyb = (char *)list_entry(b, struct hmap_entry_t, head)->key;
	return strcmp(keya, keyb);
}

void hmap_sort(struct hmap_t * m)
{
	if(m)
		lsort(NULL, &m->list, hmap_compare);
}

void * hmap_search(struct hmap_t * m, const char * key)
{
	struct hmap_entry_t * pos;
	struct hlist_node * n;

	if(!m || !key)
		return NULL;

	hlist_for_each_entry_safe(pos, n, &m->hash[shash(key) & (m->size - 1)], node)
	{
		if(strcmp(pos->key, key) == 0)
			return pos->value;
	}
	return NULL;
}
