// NOTE(hugo): DEFER is optimized away with O1 ; copying /action/ makes smaller asm than taking a ref
template<typename DEFER_Action>
struct DEFER_Container{
	DEFER_Container(DEFER_Action&& input_action) : action(input_action){}
	~DEFER_Container(){ action(); }
	DEFER_Action action;
};

template<typename T>
T max(T a, T b){ return (a > b) ? a : b; }
template<typename T>
T min(T a, T b){ return (a < b) ? a : b; }
template<typename T>
T clamp(T x, T min, T max){ return ::min(::max(x, min), max); }
template<typename T>
T abs(T x){ return x < (T)0 ? -x : x; }

constexpr size_t Kilobytes( size_t size ){ return (size_t)1024 * size; }
constexpr size_t Megabytes( size_t size ){ return (size_t)1024 * 1024 * size; }
constexpr size_t Gigabytes( size_t size ){ return (size_t)1024 * 1024 * 1024 * size; }

// REF:
// https://preshing.com/20120515/memory-reordering-caught-in-the-act/ [StoreLoad reordering]
// https://github.com/microsoft/STL/blob/main/stl/inc/atomic [std::atomic implementation]

template<typename T>
T Atomic<T>::get(){
	static_assert(sizeof(T) >= 1 && sizeof(T) <= 8);

	if constexpr (sizeof(T) == 1){
		static_assert(sizeof(T) == sizeof(char));
		char out = atomic_read((char volatile*)&value);
		return *(T*)&out;
	}
	else if constexpr (sizeof(T) == 2){
		static_assert(sizeof(T) == sizeof(short));
		short out = atomic_read((short volatile*)&value);
		return *(T*)&out;
	}
	else if constexpr (sizeof(T) == 4){
		static_assert(sizeof(T) == sizeof(int));
		uint out = atomic_read((uint volatile*)&value);
		return *(T*)&out;
	}
	else if constexpr (sizeof(T) == 8){
		static_assert(sizeof(T) == sizeof(int64));
		int64 out = atomic_read((int64 volatile*)&value);
		return *(T*)&out;
	}
}

template<typename T>
void Atomic<T>::set(T new_value){
	exchange(new_value);
}

template<typename T>
T Atomic<T>::increment(){
	static_assert(sizeof(T) >= 2 && sizeof(T) <= 8);

	if constexpr (sizeof(T) == 2){
		static_assert(sizeof(T) == sizeof(short));
		short out = atomic_increment((short volatile*)&value);
		return *(T*)&out;
	}
	else if constexpr (sizeof(T) == 4){
		static_assert(sizeof(T) == sizeof(int));
		uint out = atomic_increment((uint volatile*)&value);
		return *(T*)&out;
	}
	else if constexpr (sizeof(T) == 8){
		static_assert(sizeof(T) == sizeof(int64));
		int64 out = atomic_increment((int64 volatile*)&value);
		return *(T*)&out;
	}
}

template<typename T>
T Atomic<T>::decrement(){
	static_assert(sizeof(T) >= 2 && sizeof(T) <= 8);

	if constexpr (sizeof(T) == 2){
		static_assert(sizeof(T) == sizeof(short));
		short out = atomic_decrement((short volatile*)&value);
		return *(T*)&out;
	}
	else if constexpr (sizeof(T) == 4){
		static_assert(sizeof(T) == sizeof(int));
		uint out = atomic_decrement((uint volatile*)&value);
		return *(T*)&out;
	}
	else if constexpr (sizeof(T) == 8){
		static_assert(sizeof(T) == sizeof(int64));
		int64 out = atomic_decrement((int64 volatile*)&value);
		return *(T*)&out;
	}
}

template<typename T>
T Atomic<T>::exchange(T new_value){
	static_assert(sizeof(T) >= 1 && sizeof(T) <= 8);

	if constexpr (sizeof(T) == 1){
		static_assert(sizeof(T) == sizeof(char));
		char out = atomic_exchange((char volatile*)&value, *(char*)&new_value);
		return *(T*)&out;
	}
	else if constexpr (sizeof(T) == 2){
		static_assert(sizeof(T) == sizeof(short));
		short out = atomic_exchange((short volatile*)&value, *(short*)&new_value);
		return *(T*)&out;
	}
	else if constexpr (sizeof(T) == 4){
		static_assert(sizeof(T) == sizeof(int));
		uint out = atomic_exchange((uint volatile*)&value, *(uint*)&new_value);
		return *(T*)&out;
	}
	else if constexpr (sizeof(T) == 8){
		static_assert(sizeof(T) == sizeof(int64));
		int64 out = atomic_exchange((int64 volatile*)&value, *(int64*)&new_value);
		return *(T*)&out;
	}
}

template<typename T>
T Atomic<T>::compare_exchange(T expected, T new_value){
	static_assert(sizeof(T) >= 1 && sizeof(T) <= 8);

	if constexpr (sizeof(T) == 1){
		static_assert(sizeof(T) == sizeof(char));
		char out = atomic_compare_exchange((char volatile*)&value, *(char*)&new_value, *(char*)&expected);
		return *(T*)&out;
	}
	else if constexpr (sizeof(T) == 2){
		static_assert(sizeof(T) == sizeof(short));
		short out = atomic_compare_exchange((short volatile*)&value, *(short*)&new_value, *(short*)&expected);
		return *(T*)&out;
	}
	else if constexpr (sizeof(T) == 4){
		static_assert(sizeof(T) == sizeof(int));
		uint out = atomic_compare_exchange((uint volatile*)&value, *(uint*)&new_value, *(uint*)&expected);
		return *(T*)&out;
	}
	else if constexpr (sizeof(T) == 8){
		static_assert(sizeof(T) == sizeof(int64));
		int64 out = atomic_compare_exchange((int64 volatile*)&value, *(int64*)&new_value, *(int64*)&expected);
		return *(T*)&out;
	}
}

template<typename T>
typename MuProSiCo<T>::Link* MuProSiCo<T>::reverse_order(MuProSiCo<T>::Link* head){
	Link* left = NULL;
	Link* current = head;
	while (current){
		Link* right = current->next;
		current->next = left;
		left = current;
		current = right;
	}
	return left;
}

template<typename T>
void MuProSiCo<T>::create(){
	head.set(NULL);
}

template<typename T>
void MuProSiCo<T>::destroy(){
	Link* iter = head;
	while (iter != NULL){
		Link* next = iter->next;
		free(iter);
		iter = next;
	}
}

template<typename T>
void MuProSiCo<T>::push(MuProSiCo<T>::Link* new_link){
	Link* current_head;
	do{
		current_head = head.get();
		new_link->next = current_head;
	} while (head.compare_exchange(current_head, new_link) != current_head);
}

template<typename T>
typename MuProSiCo<T>::Link* MuProSiCo<T>::get_everything_reversed(){
	return head.exchange(NULL);
}

static constexpr u64 array_raw_min_capacity = 16u;
inline u64 array_raw_next_capacity(u64 capacity){
	capacity <<= 1u;
	capacity |= capacity >> 1u;
	capacity |= capacity >> 2u;
	capacity |= capacity >> 4u;
	capacity |= capacity >> 8u;
	capacity |= capacity >> 16u;
	capacity |= capacity >> 32u;
	return max(array_raw_min_capacity, capacity + 1u);
}

template<typename T>
void array_raw<T>::create(){
	ptr = NULL;
	ptr_size = 0u;
	ptr_capacity = 0u;
}
template<typename T>
void array_raw<T>::destroy(){
	free(ptr);
}

template<typename T>
const T& array_raw<T>::operator[](const u64 index) const{
	ram_assert(index < ptr_size);
	return ptr[index];
}
template<typename T>
T& array_raw<T>::operator[](const u64 index){
	return const_cast<T&>(const_cast<const array_raw<T>*>(this)->operator[](index));
}

template<typename T>
void array_raw<T>::push(const T& v){
	if(ptr_size == ptr_capacity) set_capacity(array_raw_next_capacity(ptr_capacity));
	ptr[ptr_size++] = v;
}
template<typename T>
void array_raw<T>::pop(){
	ram_assert(ptr_size);
	--ptr_size;
}
template<typename T>
void array_raw<T>::pop(T& v){
	ram_assert(ptr_size);
	--ptr_size;
	v = ptr[ptr_size];
}
template<typename T>
void array_raw<T>::insert(u64 index, const T& v){
	ram_assert(index <= ptr_size);
	if(ptr_size == ptr_capacity) set_capacity(array_raw_next_capacity(ptr_capacity));
	if(index != ptr_size) memmove(ptr + index + 1u, ptr + index, (ptr_size - index) * sizeof(T));
	ptr[index] = v;
	++ptr_size;
}
template<typename T>
void array_raw<T>::insert_swap(u64 index, const T& v){
	ram_assert(index <= ptr_size);
	if(ptr_size == ptr_capacity) set_capacity(array_raw_next_capacity(ptr_capacity));
	if(index != ptr_size) ptr[ptr_size] = ptr[index];
	ptr[index] = v;
	++ptr_size;
}
template<typename T>
void array_raw<T>::remove(u64 index){
	ram_assert(index < ptr_size);
	if(index < (ptr_size - 1u)) memmove(ptr + index, ptr + index + 1u, (ptr_size - index - 1u) * sizeof(T));
	--ptr_size;
}
template<typename T>
void array_raw<T>::remove_swap(u64 index){
	ram_assert(index < ptr_size);
	ptr[index] = ptr[--ptr_size];
}
template<typename T>
u64 array_raw<T>::size() const{
	return ptr_size;
}
template<typename T>
u64 array_raw<T>::capacity() const{
	return ptr_capacity;
}
template<typename T>
void array_raw<T>::set_size(u64 new_size){
	ram_assert(new_size <= ptr_capacity);
	ptr_size = new_size;
}
template<typename T>
void array_raw<T>::set_capacity(u64 new_capacity){
	ram_assert(new_capacity >= ptr_size);
	void* new_ptr = realloc(ptr, new_capacity * sizeof(T));
	ptr = (T*)new_ptr;
	ptr_capacity = new_capacity;
}
template<typename T>
const T* array_raw<T>::data() const{
	return ptr;
}
template<typename T>
T* array_raw<T>::data(){
	return const_cast<T*>(const_cast<const array_raw<T>*>(this)->data());
}

template<typename T>
u64 array_raw<T>::size_bytes() const{
	return ptr_size * sizeof(T);
}
template<typename T>
u64 array_raw<T>::capacity_bytes() const{
	return ptr_capacity * sizeof(T);
}
