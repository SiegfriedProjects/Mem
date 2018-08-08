#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NUMBER_PAGES      256 
#define PAGE_BYTES        256
#define NUMBER_FRAMES     128
#define TLB_ENTRIES       16

typedef struct lru_node_t
{
	int data;
	struct lru_node_t *next;
	struct lru_node_t *prev;
} lru_node_t;

typedef struct
{
	lru_node_t *head;
	lru_node_t *tail;
} lru_queue_t;

typedef uint8_t offset;

typedef struct
{
	int page;
	offset offset;
} virtual_address;


typedef int8_t frameval_t;

typedef struct
{
	int used_frames;
	frameval_t table[NUMBER_FRAMES][PAGE_BYTES];
	int page_for_frame[NUMBER_FRAMES];
	lru_queue_t queue;
} frame_table_t;


typedef struct
{
	int frame;
	uint8_t valid;
} page_entry_t;


typedef struct
{
	page_entry_t table[NUMBER_PAGES];
} page_table_t;


typedef struct
{
	uint16_t pages[TLB_ENTRIES];
	int frames[TLB_ENTRIES];
	lru_queue_t queue;
} tlb_t;


typedef struct
{
	size_t translated;
	size_t page_faults;
	size_t tlb_hits;

} statistics_t;

static statistics_t statistics;

	void lru_begin(lru_queue_t *queue);

	void lru_insert(lru_queue_t *queue, int data);

	void lru_update(lru_queue_t *queue, int data);

	int lru_return(lru_queue_t *queue);

	void begin_read(FILE *fin, FILE *backing);

	void print_address(FILE *fin, FILE *backing, int virutal_address, frame_table_t *frames, page_table_t *page_table, tlb_t *tlb, uint8_t is_write);

	void convert(char *line, size_t length, int *value);

	virtual_address get_components(int virtual_address);

	offset get_offset(int virtual_address);

	int get_page(int virtual_address);

	void load_lru(frame_table_t *frames);

	void page_fault(page_table_t *ptable, int page, frame_table_t *ftable, FILE *backing);

	void tlb_load(tlb_t *tlb);

	int tlb_frame(tlb_t *tlb, int page_number, int *frame);

	int physical_address_table(page_table_t *ptable, virtual_address *components);

	int get_physical_address(int frame, offset offset);

int main(int argc, char *argv[])
{

	FILE *addr;
	addr = fopen("addresses.txt", "r");

	FILE *back_store;
	back_store = fopen("BACKING_STORE.bin", "r");

	begin_read(addr, back_store);

    fclose(addr);
	fclose(back_store);

}
void lru_begin(lru_queue_t *queue)
{
	queue->head = malloc(sizeof *queue->head);
	queue->head->next = NULL;
	queue->head->prev = NULL;
	queue->tail = queue->head;
}


void lru_insert(lru_queue_t *queue, int data)
{
	lru_node_t *node = malloc(sizeof *node);
	node->data = data;
	node->next = NULL;
	node->prev = queue->head;
	queue->head->next = node;
	queue->head = node;
}

void lru_update(lru_queue_t *queue, int data)
{
	if (queue->head->data == data)
	{
		return;
	}

	lru_node_t *curr = queue->head->prev;
	while (curr->data != data)
	{
		curr = curr->prev;
	}

	curr->prev->next = curr->next;
	curr->next->prev = curr->prev;
	curr->prev = queue->head;
	queue->head->next = curr;
	curr->next = NULL;
	queue->head = curr;
}



int lru_return(lru_queue_t *queue)
{
	return queue->tail->next->data;
}

void begin_read(FILE *addr, FILE *back_store)
{
	frame_table_t frames;
	load_lru(&frames);
	page_table_t page_table = {0};
	tlb_t tlb;
	tlb_load(&tlb);
	
	char *line = NULL;
	size_t size = 0;
	ssize_t chars_read;
	while ((chars_read = getline(&line, &size, addr)) > 0)
	{

		chars_read = (chars_read-2);
		int address;
		convert(line, chars_read, &address);
		print_address(addr, back_store, address, &frames, &page_table, &tlb, 'W');

	}
	fprintf(stdout, "\nPercentage of Page Faults = %lf\n", (((double) statistics.page_faults / statistics.translated)*100));
	fprintf(stdout, "TLB Hit Rate = %lf\n", (double) statistics.tlb_hits / statistics.translated);

	free(line);
}

void print_address(FILE *fin, FILE *backing, int address, frame_table_t *frames, page_table_t *page_table, tlb_t *tlb, uint8_t is_write)
{
	virtual_address components = get_components(address);
	
	int frame;
	int phys_addr;
	int tlb_entry;
	if ((tlb_entry = tlb_frame(tlb, components.page, &frame)) != TLB_ENTRIES)
	{
		phys_addr = get_physical_address(frame, components.offset);

	}
	else 
	{
		page_fault(page_table, components.page, frames, backing);


		phys_addr = physical_address_table(page_table, &components);

		//update the tlb
		tlb_entry = lru_return(&tlb->queue);
		tlb->pages[tlb_entry] = components.page;
		tlb->frames[tlb_entry] = page_table->table[components.page].frame;
	}

	frameval_t memval = *((frameval_t *) frames->table + phys_addr);
	fprintf(stdout, "Virtual address: %u Physical address: %u Value: %d\n", address, phys_addr, memval);

	lru_update(&tlb->queue, tlb_entry);//update tlb

	lru_update(&frames->queue, page_table->table[components.page].frame);

	statistics.translated++;

}

void convert(char *s, size_t length, int *value)
{
    *value = 0;
    size_t power10;
    size_t i;
    for (i = length - 1, power10 = 1; i < SIZE_MAX; i--, power10 *= 10)
    {

        *value += (s[i] - 48) * power10;
    }

}

virtual_address get_components(int address)
{
	virtual_address components = { get_page(address), get_offset(address) };
	return components;
}

int get_page(int address)
{
	return (address >> sizeof(offset) * 8) & NUMBER_PAGES - 1;
}

offset get_offset(int address)
{
	return address & 255;
}


void load_lru(frame_table_t *frames)
{
	frames->used_frames = 0;
	lru_begin(&frames->queue);
	size_t i;
	for (i = 0; i < NUMBER_FRAMES; i++)
	{
		lru_insert(&frames->queue, NUMBER_FRAMES - i - 1);
	}
}

void page_fault(page_table_t *ptable, int page, frame_table_t *frames, FILE *backing)
{
	if (!ptable->table[page].valid)
	{
		statistics.page_faults++;
		
		fseek(backing, page * PAGE_BYTES, SEEK_SET);

		int next_frame;
		if (frames->used_frames < NUMBER_FRAMES)
		{
			next_frame = frames->used_frames;
			frames->used_frames++;
		}
		else
		{
			next_frame = lru_return(&frames->queue);
			int prev_page = frames->page_for_frame[next_frame];
			ptable->table[prev_page].valid = 0;

		}
		fread(frames->table + next_frame, PAGE_BYTES, 1, backing);
		ptable->table[page].frame = next_frame; 
		ptable->table[page].valid = 1;
		frames->page_for_frame[next_frame] = page;

	}

}


void tlb_load(tlb_t *tlb)
{
	lru_begin(&tlb->queue);
	
	size_t i;
	for (i = 0; i < TLB_ENTRIES; i++)
	{
		tlb->pages[i] = 256;
		lru_insert(&tlb->queue, TLB_ENTRIES - i - 1);
	}
}


int tlb_frame(tlb_t *tlb, int page, int *frame)
{
	size_t i;
	for (i = 0; i < TLB_ENTRIES; i++)
	{
		if (tlb->pages[i] == page)
		{
			statistics.tlb_hits++;
			*frame = tlb->frames[i];
			return i;
		}
	}

	return TLB_ENTRIES;
}

int physical_address_table(page_table_t *ptable, virtual_address *components)
{
	return get_physical_address(ptable->table[components->page].frame, components->offset);
}

int get_physical_address(int frame, offset offset)
{
	return frame * PAGE_BYTES + offset;
}

