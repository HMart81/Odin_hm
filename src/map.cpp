// A `Map` is an unordered hash table which can allow for a key to point to multiple values
// with the use of the `multi_*` procedures.
// TODO(bill): I should probably allow the `multi_map_*` stuff to be #ifdefed out

#define MAP_ENABLE_MULTI_MAP 1

#ifndef MAP_UTIL_STUFF
#define MAP_UTIL_STUFF
// NOTE(bill): This util stuff is the same for every `Map`
struct MapFindResult {
	isize hash_index;
	isize entry_prev;
	isize entry_index;
};


struct HashKey {
	u64 key;
};
GB_STATIC_ASSERT(gb_size_of(u64) >= gb_size_of(void *));

gb_inline HashKey hashing_proc(void const *data, isize len) {
	HashKey h = {};
	h.key = fnv64a(data, len);
	return h;
}

gb_inline HashKey hash_pointer(void const *ptr) {
	HashKey h = {};
	h.key = cast(u64)cast(uintptr)ptr;
	return h;
}

gb_inline HashKey hash_integer(u64 u) {
	HashKey h = {};
	h.key = u;
	return h;
}
gb_inline HashKey hash_f64(f64 f) {
	HashKey h = {};
	h.key = bit_cast<u64>(f);
	return h;
}

gb_inline bool hash_key_equal(HashKey a, HashKey b) {
	return a.key == b.key;
}
gb_inline bool operator==(HashKey a, HashKey b) { return hash_key_equal(a, b); }
gb_inline bool operator!=(HashKey a, HashKey b) { return !hash_key_equal(a, b); }

#endif

template <typename T>
struct MapEntry {
	HashKey  key;
	isize    next;
	T        value;
};

template <typename T>
struct Map {
	Slice<isize>        hashes;
	Array<MapEntry<T> > entries;
};


template <typename T> void map_init             (Map<T> *h, gbAllocator a, isize capacity = 16);
template <typename T> void map_destroy          (Map<T> *h);
template <typename T> T *  map_get              (Map<T> *h, HashKey const &key);
template <typename T> T &  map_must_get         (Map<T> *h, HashKey const &key);
template <typename T> void map_set              (Map<T> *h, HashKey const &key, T const &value);
template <typename T> void map_remove           (Map<T> *h, HashKey const &key);
template <typename T> void map_clear            (Map<T> *h);
template <typename T> void map_grow             (Map<T> *h);
template <typename T> void map_rehash           (Map<T> *h, isize new_count);

#if MAP_ENABLE_MULTI_MAP
// Mutlivalued map procedure
template <typename T> MapEntry<T> * multi_map_find_first(Map<T> *h, HashKey const &key);
template <typename T> MapEntry<T> * multi_map_find_next (Map<T> *h, MapEntry<T> *e);

template <typename T> isize multi_map_count     (Map<T> *h, HashKey const &key);
template <typename T> void  multi_map_get_all   (Map<T> *h, HashKey const &key, T *items);
template <typename T> void  multi_map_insert    (Map<T> *h, HashKey const &key, T const &value);
template <typename T> void  multi_map_remove    (Map<T> *h, HashKey const &key, MapEntry<T> *e);
template <typename T> void  multi_map_remove_all(Map<T> *h, HashKey const &key);
#endif

template <typename T>
gb_inline void map_init(Map<T> *h, gbAllocator a, isize capacity) {
	capacity = next_pow2_isize(capacity);
	slice_init(&h->hashes,  a, capacity);
	array_init(&h->entries, a, 0, capacity);
	for (isize i = 0; i < capacity; i++) {
		h->hashes.data[i] = -1;
	}
}

template <typename T>
gb_inline void map_destroy(Map<T> *h) {
	slice_free(&h->hashes, h->entries.allocator);
	array_free(&h->entries);
}

template <typename T>
gb_internal isize map__add_entry(Map<T> *h, HashKey const &key) {
	MapEntry<T> e = {};
	e.key = key;
	e.next = -1;
	array_add(&h->entries, e);
	return h->entries.count-1;
}

template <typename T>
gb_internal MapFindResult map__find(Map<T> *h, HashKey const &key) {
	MapFindResult fr = {-1, -1, -1};
	if (h->hashes.count == 0) {
		return fr;
	}
	fr.hash_index = key.key & (h->hashes.count-1);
	fr.entry_index = h->hashes.data[fr.hash_index];
	while (fr.entry_index >= 0) {
		if (hash_key_equal(h->entries.data[fr.entry_index].key, key)) {
			return fr;
		}
		fr.entry_prev = fr.entry_index;
		fr.entry_index = h->entries.data[fr.entry_index].next;
	}
	return fr;
}

template <typename T>
gb_internal MapFindResult map__find_from_entry(Map<T> *h, MapEntry<T> *e) {
	MapFindResult fr = {-1, -1, -1};
	if (h->hashes.count == 0) {
		return fr;
	}
	fr.hash_index  = e->key.key & (h->hashes.count-1);
	fr.entry_index = h->hashes.data[fr.hash_index];
	while (fr.entry_index >= 0) {
		if (&h->entries.data[fr.entry_index] == e) {
			return fr;
		}
		fr.entry_prev = fr.entry_index;
		fr.entry_index = h->entries.data[fr.entry_index].next;
	}
	return fr;
}

template <typename T>
gb_internal b32 map__full(Map<T> *h) {
	return 0.75f * h->hashes.count <= h->entries.count;
}

template <typename T>
gb_inline void map_grow(Map<T> *h) {
	isize new_count = gb_max(h->hashes.count<<1, 16);
	map_rehash(h, new_count);
}

template <typename T>
void map_rehash(Map<T> *h, isize new_count) {
	isize i, j;
	Map<T> nh = {};
	new_count = next_pow2_isize(new_count);
	nh.hashes = h->hashes;
	nh.entries.allocator = h->entries.allocator;
	slice_resize(&nh.hashes, h->entries.allocator, new_count);
	for (i = 0; i < new_count; i++) {
		nh.hashes.data[i] = -1;
	}
	array_reserve(&nh.entries, ARRAY_GROW_FORMULA(h->entries.count));
	for (i = 0; i < h->entries.count; i++) {
		MapEntry<T> *e = &h->entries.data[i];
		MapFindResult fr;
		if (nh.hashes.count == 0) {
			map_grow(&nh);
		}
		fr = map__find(&nh, e->key);
		j = map__add_entry(&nh, e->key);
		if (fr.entry_prev < 0) {
			nh.hashes.data[fr.hash_index] = j;
		} else {
			nh.entries.data[fr.entry_prev].next = j;
		}
		nh.entries.data[j].next = fr.entry_index;
		nh.entries.data[j].value = e->value;
		if (map__full(&nh)) {
			map_grow(&nh);
		}
	}
	array_free(&h->entries);
	*h = nh;
}

template <typename T>
T *map_get(Map<T> *h, HashKey const &key) {
	isize index = map__find(h, key).entry_index;
	if (index >= 0) {
		return &h->entries.data[index].value;
	}
	return nullptr;
}

template <typename T>
T &map_must_get(Map<T> *h, HashKey const &key) {
	isize index = map__find(h, key).entry_index;
	GB_ASSERT(index >= 0);
	return h->entries.data[index].value;
}

template <typename T>
void map_set(Map<T> *h, HashKey const &key, T const &value) {
	isize index;
	MapFindResult fr;
	if (h->hashes.count == 0) {
		map_grow(h);
	}
	fr = map__find(h, key);
	if (fr.entry_index >= 0) {
		index = fr.entry_index;
	} else {
		index = map__add_entry(h, key);
		if (fr.entry_prev >= 0) {
			h->entries.data[fr.entry_prev].next = index;
		} else {
			h->hashes.data[fr.hash_index] = index;
		}
	}
	h->entries.data[index].value = value;

	if (map__full(h)) {
		map_grow(h);
	}
}


template <typename T>
void map__erase(Map<T> *h, MapFindResult const &fr) {
	MapFindResult last;
	if (fr.entry_prev < 0) {
		h->hashes.data[fr.hash_index] = h->entries.data[fr.entry_index].next;
	} else {
		h->entries.data[fr.entry_prev].next = h->entries.data[fr.entry_index].next;
	}
	if (fr.entry_index == h->entries.count-1) {
		array_pop(&h->entries);
		return;
	}
	h->entries.data[fr.entry_index] = h->entries.data[h->entries.count-1];
	array_pop(&h->entries);
	
	last = map__find(h, h->entries.data[fr.entry_index].key);
	if (last.entry_prev >= 0) {
		h->entries.data[last.entry_prev].next = fr.entry_index;
	} else {
		h->hashes.data[last.hash_index] = fr.entry_index;
	}
}

template <typename T>
void map_remove(Map<T> *h, HashKey const &key) {
	MapFindResult fr = map__find(h, key);
	if (fr.entry_index >= 0) {
		map__erase(h, fr);
	}
}

template <typename T>
gb_inline void map_clear(Map<T> *h) {
	array_clear(&h->entries);
	for (isize i = 0; i < h->hashes.count; i++) {
		h->hashes.data[i] = -1;
	}
}


#if MAP_ENABLE_MULTI_MAP
template <typename T>
MapEntry<T> *multi_map_find_first(Map<T> *h, HashKey const &key) {
	isize i = map__find(h, key).entry_index;
	if (i < 0) {
		return nullptr;
	}
	return &h->entries.data[i];
}

template <typename T>
MapEntry<T> *multi_map_find_next(Map<T> *h, MapEntry<T> *e) {
	isize i = e->next;
	while (i >= 0) {
		if (hash_key_equal(h->entries.data[i].key, e->key)) {
			return &h->entries.data[i];
		}
		i = h->entries.data[i].next;
	}
	return nullptr;
}

template <typename T>
isize multi_map_count(Map<T> *h, HashKey const &key) {
	isize count = 0;
	MapEntry<T> *e = multi_map_find_first(h, key);
	while (e != nullptr) {
		count++;
		e = multi_map_find_next(h, e);
	}
	return count;
}

template <typename T>
void multi_map_get_all(Map<T> *h, HashKey const &key, T *items) {
	isize i = 0;
	MapEntry<T> *e = multi_map_find_first(h, key);
	while (e != nullptr) {
		items[i++] = e->value;
		e = multi_map_find_next(h, e);
	}
}

template <typename T>
void multi_map_insert(Map<T> *h, HashKey const &key, T const &value) {
	MapFindResult fr;
	isize i;
	if (h->hashes.count == 0) {
		map_grow(h);
	}
	// Make
	fr = map__find(h, key);
	i = map__add_entry(h, key);
	if (fr.entry_prev < 0) {
		h->hashes.data[fr.hash_index] = i;
	} else {
		h->entries.data[fr.entry_prev].next = i;
	}
	h->entries.data[i].next = fr.entry_index;
	h->entries.data[i].value = value;
	// Grow if needed
	if (map__full(h)) {
		map_grow(h);
	}
}

template <typename T>
void multi_map_remove(Map<T> *h, HashKey const &key, MapEntry<T> *e) {
	MapFindResult fr = map__find_from_entry(h, e);
	if (fr.entry_index >= 0) {
		map__erase(h, fr);
	}
}

template <typename T>
void multi_map_remove_all(Map<T> *h, HashKey const &key) {
	while (map_get(h, key) != nullptr) {
		map_remove(h, key);
	}
}
#endif
