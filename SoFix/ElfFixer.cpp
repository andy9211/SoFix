#include "ElfFixer.h"
#include "linker.h"
#include <QDebug>
#include "Util.h"

#define QSTR8BIT(s) (QString::fromLocal8Bit(s))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define MIN(a, b) (((a) < (b)) ? (a) : (b))

#define DEBUG qDebug
#define DL_ERR qDebug

const char ElfFixer::strtab[] =
"\0.dynsym\0.dynstr\0.hash\0.rel.dyn\0.rel.plt\0.plt\0.text\0.ARM.exidx\0.rodata\0"
".fini_array\0.init_array\0.data.rel.ro\0.dynamic\0.got\0.data\0.bss\0.shstrtab\0";

Elf32_Word ElfFixer::GetShdrName(int idx)
{
	Elf32_Word ret = 0;
	const char *str = strtab;
	for (int i = 0; i < idx; i++)
	{
		ret += strlen(str) + 1;
		str += strlen(str) + 1;
	}

	return ret;
}

ElfFixer::ElfFixer(soinfo *si, Elf32_Ehdr ehdr, const char *name)
	: si_(si), ehdr_(ehdr), phdr_(nullptr), phnum_(0)
{
	if (name)
	{
		name_ = strdup(name);
		file_.setFileName(QSTR8BIT(name));
	}

	memset(shdrs_, 0, sizeof(shdrs_));
}

ElfFixer::~ElfFixer()
{
	if (name_)
	{
		free(name_);
	}
	file_.close();
}

bool ElfFixer::fix()
{
	return fixPhdr() &&
		fixEhdr() &&
		fixShdr() &&
		showAll();
}

bool ElfFixer::write()
{
	if (!file_.open(QIODevice::ReadWrite))
	{
		return false;
	}

	const Elf32_Phdr* phdr = phdr_;
	const Elf32_Phdr* phdr_limit = phdr + phnum_;

	//���ǵ������ݿ�������ʱ���ı�, �����dump���ļ��ж�ȡ
	for (phdr = phdr_; phdr < phdr_limit; phdr++)
	{
		file_.seek(PAGE_START(phdr->p_offset));
		file_.write((char *)(PAGE_START(si_->load_bias + phdr->p_vaddr)), PAGE_END(phdr->p_filesz));
	}

	//Elfͷ��Ӧ���ڶ�����д��֮��д��, ���ⱻ�����ݸ���
	file_.seek(0);
	file_.write((char *)&ehdr_, sizeof(Elf32_Ehdr));
	file_.seek(ehdr_.e_phoff);
	file_.write((char *)phdr_, ehdr_.e_phnum + sizeof(Elf32_Phdr));

	//д���ͷ
	file_.seek(ehdr_.e_shoff);
	file_.write((char *)&shdrs_[SI_NULL], sizeof(Elf32_Shdr));
	file_.write((char *)&shdrs_[SI_DYNSYM], sizeof(Elf32_Shdr));
	file_.write((char *)&shdrs_[SI_DYNSTR], sizeof(Elf32_Shdr));
	file_.write((char *)&shdrs_[SI_HASH], sizeof(Elf32_Shdr));
	file_.write((char *)&shdrs_[SI_RELDYN], sizeof(Elf32_Shdr));
	file_.write((char *)&shdrs_[SI_RELPLT], sizeof(Elf32_Shdr));
	file_.write((char *)&shdrs_[SI_PLT], sizeof(Elf32_Shdr));
	file_.write((char *)&shdrs_[SI_TEXT], sizeof(Elf32_Shdr));
	file_.write((char *)&shdrs_[SI_ARMEXIDX], sizeof(Elf32_Shdr));
	file_.write((char *)&shdrs_[SI_RODATA], sizeof(Elf32_Shdr));
	file_.write((char *)&shdrs_[SI_FINI_ARRAY], sizeof(Elf32_Shdr));
	file_.write((char *)&shdrs_[SI_INIT_ARRAY], sizeof(Elf32_Shdr));
	file_.write((char *)&shdrs_[SI_DATA_REL_RO], sizeof(Elf32_Shdr));
	file_.write((char *)&shdrs_[SI_DYNAMIC], sizeof(Elf32_Shdr));
	file_.write((char *)&shdrs_[SI_GOT], sizeof(Elf32_Shdr));
	file_.write((char *)&shdrs_[SI_DATA], sizeof(Elf32_Shdr));
	file_.write((char *)&shdrs_[SI_BSS], sizeof(Elf32_Shdr));
	file_.write((char *)&shdrs_[SI_SHSTRTAB], sizeof(Elf32_Shdr));

	//д���ͷ����
	file_.seek(shdrs_[SI_SHSTRTAB].sh_offset);
	file_.write(strtab, shdrs_[SI_SHSTRTAB].sh_size);

	return true;
}

bool ElfFixer::fixEhdr()
{
	//��shdr���ڿɼ��ض����ݵ�β��
	const Elf32_Phdr* phdr = si_->phdr;
	const Elf32_Phdr* phdr_limit = phdr + si_->phnum;

	Elf32_Off max_off = 0;

	for (phdr = si_->phdr; phdr < phdr_limit; phdr++)
	{
		if (PAGE_END(phdr->p_offset + phdr->p_filesz) > max_off)
		{
			max_off = PAGE_END(phdr->p_offset + phdr->p_filesz);
		}
	}

	ehdr_.e_shoff = max_off;
	ehdr_.e_shentsize = sizeof(Elf32_Shdr);
	ehdr_.e_shnum = SI_MAX;
	ehdr_.e_shstrndx = SI_SHSTRTAB;

	//��shdrβ���Ӹ������Ʊ�
	shdrs_[SI_SHSTRTAB].sh_name = GetShdrName(SI_SHSTRTAB);
	shdrs_[SI_SHSTRTAB].sh_type = SHT_STRTAB;
	shdrs_[SI_SHSTRTAB].sh_flags = 0;
	shdrs_[SI_SHSTRTAB].sh_addr = 0;
	shdrs_[SI_SHSTRTAB].sh_offset = ehdr_.e_shoff + ehdr_.e_shnum * sizeof(Elf32_Shdr);
	shdrs_[SI_SHSTRTAB].sh_size = sizeof(strtab);
	shdrs_[SI_SHSTRTAB].sh_link = 0;
	shdrs_[SI_SHSTRTAB].sh_info = 0;
	shdrs_[SI_SHSTRTAB].sh_addralign = 1;
	shdrs_[SI_SHSTRTAB].sh_entsize = 0;

	return true;
}

bool ElfFixer::fixPhdr()
{
	if (si_)
	{
		phdr_ = si_->phdr;
		phnum_ = si_->phnum;
		return true;
	}

	return false;
}

bool ElfFixer::fixShdr()
{
	return fixShdrFromPhdr() &&
		fixShdrFromDynamic() &&
		fixShdrFromShdr();
}

bool ElfFixer::showAll()
{
	DEBUG("Name\tAddr\tOff\tSize\tEs\n");
	for (int i = 0; i < SI_MAX; i++)
	{
		DEBUG("%s\t%08X\t%08X\t%08X\t%08X\n",
			strtab + shdrs_[i].sh_name, shdrs_[i].sh_addr,
			shdrs_[i].sh_offset, shdrs_[i].sh_size, shdrs_[i].sh_entsize);
	}

	return true;
}

bool ElfFixer::fixShdrFromPhdr()
{
	//�޸�.dynamic: ֱ�Ӷ�ȡ��, ��С������DT_NULL
	phdr_table_get_dynamic_section(phdr_, phnum_, si_->load_bias, &si_->dynamic, NULL, NULL);
	shdrs_[SI_DYNAMIC].sh_name = GetShdrName(SI_DYNAMIC);
	shdrs_[SI_DYNAMIC].sh_type = SHT_DYNAMIC;
	shdrs_[SI_DYNAMIC].sh_flags = SHF_WRITE | SHF_ALLOC;
	shdrs_[SI_DYNAMIC].sh_addr = (Elf32_Addr)si_->dynamic - si_->load_bias;
	shdrs_[SI_DYNAMIC].sh_offset = addrToOff(shdrs_[SI_DYNAMIC].sh_addr);
	//dynamic_.sh_size = 0;		//������DT_NULL����ȷ����С
	shdrs_[SI_DYNAMIC].sh_link = SI_DYNSTR;
	shdrs_[SI_DYNAMIC].sh_info = 0;
	shdrs_[SI_DYNAMIC].sh_addralign = 4;
	shdrs_[SI_DYNAMIC].sh_entsize = sizeof(Elf32_Dyn);

	//�޸�.arm.exidx: ֱ�Ӷ�ȡ��
	(void)phdr_table_get_arm_exidx(phdr_, phnum_, si_->load_bias, &si_->ARM_exidx, &si_->ARM_exidx_count);
	if (si_->ARM_exidx)
	{
		shdrs_[SI_ARMEXIDX].sh_name = GetShdrName(SI_ARMEXIDX);
		shdrs_[SI_ARMEXIDX].sh_type = SHT_AMMEXIDX;
		shdrs_[SI_ARMEXIDX].sh_flags = SHF_ALLOC | SHF_LINK_ORDER;
		shdrs_[SI_ARMEXIDX].sh_addr = (Elf32_Addr)si_->ARM_exidx - si_->load_bias;
		shdrs_[SI_ARMEXIDX].sh_offset = addrToOff(shdrs_[SI_ARMEXIDX].sh_addr);
		shdrs_[SI_ARMEXIDX].sh_size = si_->ARM_exidx_count * 8;
		shdrs_[SI_ARMEXIDX].sh_link = SI_TEXT;
		shdrs_[SI_ARMEXIDX].sh_info = 0;
		shdrs_[SI_ARMEXIDX].sh_addralign = 4;
		shdrs_[SI_ARMEXIDX].sh_entsize = 8;
	}

	return true;
}

bool ElfFixer::fixShdrFromDynamic()
{
	//����.dynamic, �޸����½�:
	//.hash: ֱ�Ӷ�ȡDT_HASH
	//.dynstr: ��ȡDT_STRTAB��ȡ��ʼλ��, ��С�����øýڵ�.dynamic(DT_NEED), dynsym��ȡ
	//.dynsym: ��ȡDT_SYMTAB��ȡ��ʼλ��, ��С�����øýڵ�.rel.dyn, .rel.plt, .hash��ȡ
	//.rel.dyn: ֱ�Ӷ�ȡDT_REL, DT_RELSZ���ɻ�ȡ��ʼλ�úʹ�С
	//.rel.plt: ֱ�Ӷ�ȡDT_JMPREL, DT_PLTRELSZ���ɻ�ȡ��ʼλ�úʹ�С
	//.init_array: ֱ�Ӷ�ȡDT_INIT_ARRAY, DT_INIT_ARRAYSZ���ɻ�ȡ��ʼλ�úʹ�С
	//.fini_array: ֱ�Ӷ�ȡDT_FINI_ARRAY, DT_FINI_ARRAYSZ���ɻ�ȡ��ʼλ�úʹ�С


	/* "base" might wrap around UINT32_MAX. */
	Elf32_Addr base = si_->load_bias;
	shdrs_[SI_DYNAMIC].sh_size = 1;	//DT_NULL
	shdrs_[SI_DYNSTR].sh_size = 0;
	uint32_t needed_count = 0; //��¼DT_NEEDED������
	// Extract useful information from dynamic section. ��ȡ��̬���е�������Ϣ������DT_
	// ��ȡ����entry������(��ַ��ֵ), DT_NULLΪ�ýڵĽ�����־

	for (Elf32_Dyn* d = si_->dynamic; d->d_tag != DT_NULL; ++d)
	{
		shdrs_[SI_DYNAMIC].sh_size += sizeof(Elf32_Dyn);	//����DT�õ�dynamic_��׼ȷ��С
		switch (d->d_tag)
		{
		case DT_HASH:
			si_->nbucket = ((unsigned *)(base + d->d_un.d_ptr))[0];
			si_->nchain = ((unsigned *)(base + d->d_un.d_ptr))[1];
			si_->bucket = (unsigned *)(base + d->d_un.d_ptr + 8);
			si_->chain = (unsigned *)(base + d->d_un.d_ptr + 8 + si_->nbucket * 4);

			shdrs_[SI_HASH].sh_name = GetShdrName(SI_HASH);
			shdrs_[SI_HASH].sh_type = SHT_HASH;
			shdrs_[SI_HASH].sh_flags = SHF_ALLOC;
			shdrs_[SI_HASH].sh_addr = d->d_un.d_ptr;
			shdrs_[SI_HASH].sh_offset = addrToOff(shdrs_[SI_HASH].sh_addr);
			shdrs_[SI_HASH].sh_size = (2 + si_->nbucket + si_->nchain) * sizeof(Elf32_Word);
			shdrs_[SI_HASH].sh_link = ShIdx::SI_DYNSYM;	//.dynsym�Ľ�����
			shdrs_[SI_HASH].sh_info = 0;
			shdrs_[SI_HASH].sh_addralign = 4;
			shdrs_[SI_HASH].sh_entsize = sizeof(Elf32_Word);
			break;
		case DT_STRTAB:
			si_->strtab = (const char *)(base + d->d_un.d_ptr);

			shdrs_[SI_DYNSTR].sh_name = GetShdrName(SI_DYNSTR);
			shdrs_[SI_DYNSTR].sh_type = SHT_STRTAB;
			shdrs_[SI_DYNSTR].sh_flags = SHF_ALLOC;
			shdrs_[SI_DYNSTR].sh_addr = d->d_un.d_ptr;
			shdrs_[SI_DYNSTR].sh_offset = addrToOff(shdrs_[SI_DYNSTR].sh_addr);
			//shdrs_[SI_DYNSTR].sh_size = 0;	//��������ֶβ��Ǳ����, ��DT_STRSZ��ȡ��С���ܲ�׼ȷ
			shdrs_[SI_DYNSTR].sh_link = 0;
			shdrs_[SI_DYNSTR].sh_info = 0;
			shdrs_[SI_DYNSTR].sh_addralign = 1;
			shdrs_[SI_DYNSTR].sh_entsize = 0;
			break;
		case DT_STRSZ:
			//shdrs_[SI_DYNSTR].sh_size = d->d_un.d_val;	//��DT_STRSZ��ȡ��С���ܲ�׼ȷ, ��������ͨ�����øý�(.symtab, .dynamic)�ķ�Χ��ȷ����С
			break;
		case DT_SYMTAB:
			si_->symtab = (Elf32_Sym *)(base + d->d_un.d_ptr);

			shdrs_[SI_DYNSYM].sh_name = GetShdrName(SI_DYNSYM);
			shdrs_[SI_DYNSYM].sh_type = SHT_DYNSYM;
			shdrs_[SI_DYNSYM].sh_flags = SHF_ALLOC;
			shdrs_[SI_DYNSYM].sh_addr = d->d_un.d_ptr;
			shdrs_[SI_DYNSYM].sh_offset = addrToOff(shdrs_[SI_DYNSYM].sh_addr);
			shdrs_[SI_DYNSYM].sh_size = 0;		//�������ݷ���HASH������
			shdrs_[SI_DYNSYM].sh_link = ShIdx::SI_DYNSTR;	//.dynstr�Ľ�����
			shdrs_[SI_DYNSYM].sh_info = 1;	//���һ���ֲ����ŵķ��ű�����ֵ��һ, ��ʱ��1
			shdrs_[SI_DYNSYM].sh_addralign = 4;
			shdrs_[SI_DYNSYM].sh_entsize = sizeof(Elf32_Sym);
			break;
		case DT_JMPREL:
			si_->plt_rel = (Elf32_Rel*)(base + d->d_un.d_ptr);

			shdrs_[SI_RELPLT].sh_name = GetShdrName(SI_RELPLT);
			shdrs_[SI_RELPLT].sh_type = SHT_REL;
			shdrs_[SI_RELPLT].sh_flags = SHF_ALLOC | SHF_INFO_LINK;
			shdrs_[SI_RELPLT].sh_addr = d->d_un.d_ptr;
			shdrs_[SI_RELPLT].sh_offset = addrToOff(shdrs_[SI_RELPLT].sh_addr);
			//shdrs_[SI_RELPLT].sh_size = 0;		//����DT_PLTRELSZ��ȡ׼ȷ��С
			shdrs_[SI_RELPLT].sh_link = ShIdx::SI_DYNSYM;	//.dynsym�Ľ�����
			shdrs_[SI_RELPLT].sh_info = ShIdx::SI_GOT;		//.got�Ľ�����
			shdrs_[SI_RELPLT].sh_addralign = 4;
			shdrs_[SI_RELPLT].sh_entsize = sizeof(Elf32_Rel);
			break;
		case DT_PLTRELSZ:
			si_->plt_rel_count = d->d_un.d_val / sizeof(Elf32_Rel);

			shdrs_[SI_RELPLT].sh_size = d->d_un.d_val;	//����DT_PLTRELSZ��ȡ.rel.plt׼ȷ��С
			break;
		case DT_REL:
			si_->rel = (Elf32_Rel*)(base + d->d_un.d_ptr);

			shdrs_[SI_RELDYN].sh_name = GetShdrName(SI_RELDYN);
			shdrs_[SI_RELDYN].sh_type = SHT_REL;
			shdrs_[SI_RELDYN].sh_flags = SHF_ALLOC;
			shdrs_[SI_RELDYN].sh_addr = d->d_un.d_ptr;
			shdrs_[SI_RELDYN].sh_offset = addrToOff(shdrs_[SI_RELDYN].sh_addr);
			//shdrs_[SI_RELDYN].sh_size = 0;	//������DT_RELSZȷ��׼ȷ��С
			shdrs_[SI_RELDYN].sh_link = ShIdx::SI_DYNSYM;	//.dynsym�Ľ�����
			shdrs_[SI_RELDYN].sh_info = 0;
			shdrs_[SI_RELDYN].sh_addralign = 4;
			shdrs_[SI_RELDYN].sh_entsize = sizeof(Elf32_Rel);
			break;
		case DT_RELSZ:
			si_->rel_count = d->d_un.d_val / sizeof(Elf32_Rel);

			shdrs_[SI_RELDYN].sh_size = d->d_un.d_val;	//��ȡ.rel.dyn׼ȷ��С
			break;
		case DT_PLTGOT:
			/* Save this in case we decide to do lazy binding. We don't yet. */
			si_->plt_got = (unsigned *)(base + d->d_un.d_ptr);	//_global_offset_table_�������ַ, ���Ǳ�����Ϣ, ���ֻ��������׼ȷ�޸�
			break;
		case DT_INIT_ARRAY:
			si_->init_array = reinterpret_cast<linker_function_t*>(base + d->d_un.d_ptr);
			DEBUG("%s constructors (DT_INIT_ARRAY) found at %p", si_->name, si_->init_array);

			shdrs_[SI_INIT_ARRAY].sh_name = GetShdrName(SI_INIT_ARRAY);
			shdrs_[SI_INIT_ARRAY].sh_type = SHT_INIT_ARRAY;
			shdrs_[SI_INIT_ARRAY].sh_flags = SHF_WRITE | SHF_ALLOC;
			shdrs_[SI_INIT_ARRAY].sh_addr = d->d_un.d_ptr;
			shdrs_[SI_INIT_ARRAY].sh_offset = addrToOff(shdrs_[SI_INIT_ARRAY].sh_addr);
			//shdrs_[SI_INIT_ARRAY].sh_size = 0;	//���Ը���DT_INIT_ARRAYSZ��ȡ׼ȷ��С
			shdrs_[SI_INIT_ARRAY].sh_link = 0;
			shdrs_[SI_INIT_ARRAY].sh_info = 0;
			shdrs_[SI_INIT_ARRAY].sh_addralign = 4;
			shdrs_[SI_INIT_ARRAY].sh_entsize = 4;
			break;
		case DT_INIT_ARRAYSZ:
			si_->init_array_count = ((unsigned)d->d_un.d_val) / sizeof(Elf32_Addr);

			shdrs_[SI_INIT_ARRAY].sh_size = d->d_un.d_val;	//��ȡ.init_array��׼ȷ��С
			break;
		case DT_FINI_ARRAY:
			si_->fini_array = reinterpret_cast<linker_function_t*>(base + d->d_un.d_ptr);
			DEBUG("%s destructors (DT_FINI_ARRAY) found at %p", si_->name, si_->fini_array);

			shdrs_[SI_FINI_ARRAY].sh_name = GetShdrName(SI_FINI_ARRAY);
			shdrs_[SI_FINI_ARRAY].sh_type = SHT_FINI_ARRAY;
			shdrs_[SI_FINI_ARRAY].sh_flags = SHF_WRITE | SHF_ALLOC;
			shdrs_[SI_FINI_ARRAY].sh_addr = d->d_un.d_ptr;
			shdrs_[SI_FINI_ARRAY].sh_offset = addrToOff(shdrs_[SI_FINI_ARRAY].sh_addr);
			//shdrs_[SI_FINI_ARRAY].sh_size = 0;	//���Ը���DT_FINI_ARRAYSZ��ȡ׼ȷ��С
			shdrs_[SI_FINI_ARRAY].sh_link = 0;
			shdrs_[SI_FINI_ARRAY].sh_info = 0;
			shdrs_[SI_FINI_ARRAY].sh_addralign = 4;
			shdrs_[SI_FINI_ARRAY].sh_entsize = 4;
			break;
		case DT_FINI_ARRAYSZ:
			si_->fini_array_count = ((unsigned)d->d_un.d_val) / sizeof(Elf32_Addr);

			shdrs_[SI_FINI_ARRAY].sh_size = d->d_un.d_val;	//��ȡ.fini_array��׼ȷ��С
			break;

		case DT_NEEDED:
			++needed_count;
			break;
		}

	}

	fixDynsym();
	fixDynstr();

	return true;
}

void ElfFixer::fixDynstr()
{
	//����.dynamic���õ��ַ�����ȷ��.dynstr�ڵĴ�С
	for (Elf32_Dyn* d = si_->dynamic; d->d_tag != DT_NULL; ++d)
	{
		if (d->d_tag == DT_NEEDED)
		{
			const char* library_name = si_->strtab + d->d_un.d_val; //so������
			shdrs_[SI_DYNSTR].sh_size = MAX(shdrs_[SI_DYNSTR].sh_size, d->d_un.d_val + strlen(library_name) + 1);
		}
	}
}

bool ElfFixer::fixDynsym()
{
	//�޸�.dynsym, .dynstr�Ĵ�С

	shdrs_[SI_DYNSYM].sh_size = sizeof(Elf32_Sym);
	shdrs_[SI_DYNSYM].sh_info = 1;
	//ͨ��.rel.plt�����õķ�����ȷ��dynsym, dynstr�Ľڴ�С
	{
		Elf32_Rel* rel = si_->plt_rel;
		unsigned count = si_->plt_rel_count;

		Elf32_Sym* symtab = si_->symtab;
		const char* strtab = si_->strtab;

		//�����ض�λ�� DT_JMPREL/DT_REL
		for (size_t idx = 0; idx < count; ++idx, ++rel)
		{
			unsigned type = ELF32_R_TYPE(rel->r_info);
			unsigned sym = ELF32_R_SYM(rel->r_info);
			char* sym_name = NULL;
			if (type == 0) // R_*_NONE
			{
				continue;
			}
			if (sym != 0)
			{
				sym_name = (char *)(strtab + symtab[sym].st_name); //��ȡ������
				shdrs_[SI_DYNSYM].sh_size = MAX(shdrs_[SI_DYNSYM].sh_size, (sym + 1) * sizeof(Elf32_Sym));
				shdrs_[SI_DYNSTR].sh_size = MAX(shdrs_[SI_DYNSTR].sh_size, symtab[sym].st_name + strlen(sym_name) + 1);
			}

		}
	}

	//ͨ��.rel.dyn�����õķ�����ȷ��dynsym, dynstr�Ľڴ�С
	{
		Elf32_Rel* rel = si_->rel;
		unsigned count = si_->rel_count;

		Elf32_Sym* symtab = si_->symtab;
		const char* strtab = si_->strtab;

		//�����ض�λ�� DT_JMPREL/DT_REL
		for (size_t idx = 0; idx < count; ++idx, ++rel)
		{
			unsigned type = ELF32_R_TYPE(rel->r_info);
			unsigned sym = ELF32_R_SYM(rel->r_info);
			char* sym_name = NULL;
			if (type == 0) // R_*_NONE
			{
				continue;
			}
			if (sym != 0)
			{
				sym_name = (char *)(strtab + symtab[sym].st_name); //��ȡ������
				shdrs_[SI_DYNSYM].sh_size = MAX(shdrs_[SI_DYNSYM].sh_size, (sym + 1) * sizeof(Elf32_Sym));
				shdrs_[SI_DYNSTR].sh_size = MAX(shdrs_[SI_DYNSTR].sh_size, symtab[sym].st_name + strlen(sym_name) + 1);
			}
		}
	}

	//ͨ��.hash�����õķ�����ȷ��dynsym, dynstr�Ľڴ�С
	{
		Elf32_Sym* symtab = si_->symtab;
		const char* strtab = si_->strtab;
		char* sym_name = NULL;

		//����si->bucket, ��ѯsym
		for (unsigned hash = 0; hash < si_->nbucket; hash++)
		{
			for (unsigned n = si_->bucket[hash]; n != 0; n = si_->chain[n])
			{
				sym_name = (char *)(strtab + symtab[n].st_name); //��ȡ������
				shdrs_[SI_DYNSYM].sh_size = MAX(shdrs_[SI_DYNSYM].sh_size, (n + 1) * sizeof(Elf32_Sym));
				shdrs_[SI_DYNSTR].sh_size = MAX(shdrs_[SI_DYNSTR].sh_size, symtab[n].st_name + strlen(sym_name) + 1);
			}
		}
	}

	//����dynsym_�õ�sh_info
	for (Elf32_Sym *sym = si_->symtab; sym < si_->symtab + (shdrs_[SI_DYNSYM].sh_size / sizeof(Elf32_Sym)); sym++)
	{
		if (ELF32_ST_BIND(sym->st_info) == STB_LOCAL && sym->st_shndx != SHN_UNDEF)
		{
			int local_idx = si_->symtab - sym;
			shdrs_[SI_DYNSYM].sh_info = local_idx + 1;

			//TODO: �ж������ַ���ڵĽ�, �������st_shndx(ԭ��Ϊ0�Ĳ�����), ����010ʹ��-_-
			if (sym->st_shndx != 0)
			{

			}
		}
	}

	return true;
}

//������޸�����׼ȷ, ��Ϊ��Ϣ����ȫ, ֻ�ܳ����޸�
bool ElfFixer::fixShdrFromShdr()
{
	//�޸�.plt
	//�޸���ʼ��ַ, ���ַ���: 
	//	1. ͨ��16�ֽ�������(shellcode)�ұ���������֪��֮��
	//	2. ͨ��.rel.plt��������ΪR_ARM_JUMP_SLOT�ķ�Χ��ȷ��
	//04 E0 2D E5                 STR             LR, [SP, #  - 4]!
	//04 E0 9F E5                 LDR             LR, =(_GLOBAL_OFFSET_TABLE_ - 0x5EB0)
	//0E E0 8F E0                 ADD             LR, PC, LR; _GLOBAL_OFFSET_TABLE_
	//08 F0 BE E5                 LDR             PC, [LR, #8]!; dword_0

	Elf32_Addr _GLOBAL_OFFSET_TABLE_ = 0;	//_GLOBAL_OFFSET_TABLE_�������ַ

	//�޸���С = (20 + 12 * .rel.plt��Ŀ��);
	const char plt_code[] = {
		'\x4', '\xE0', '\x2D', '\xE5',
		'\x4', '\xE0', '\x9F', '\xE5',
		'\xE', '\xE0', '\x8F', '\xE0',
		'\x8', '\xF0', '\xBE', '\xE5'
	};

	//ͨ���۲췢��, .plt��, .got�ڱ�Ȼ����, ��Ϊ��Ҫ����libc��__cxa_atexit, __cxa_finalize
	int addr = Util::kmpSearch((char *)si_->base, si_->size, plt_code, 16);
	if (addr == -1)
	{
		DEBUG("kmpSearch .plt��û���ҵ�\n");
	}
	else
	{
		shdrs_[SI_PLT].sh_name = GetShdrName(SI_PLT);
		shdrs_[SI_PLT].sh_type = SHT_PROGBITS;
		shdrs_[SI_PLT].sh_flags = SHF_ALLOC | SHF_EXECINSTR;
		shdrs_[SI_PLT].sh_addr = addr;
		shdrs_[SI_PLT].sh_offset = addrToOff(shdrs_[SI_PLT].sh_addr);
		shdrs_[SI_PLT].sh_size = 20 + 12 * (shdrs_[SI_RELPLT].sh_size / sizeof(Elf32_Rel));
		shdrs_[SI_PLT].sh_link = 0;
		shdrs_[SI_PLT].sh_info = 0;
		shdrs_[SI_PLT].sh_addralign = 4;
		shdrs_[SI_PLT].sh_entsize = 0;

		_GLOBAL_OFFSET_TABLE_ = shdrs_[SI_PLT].sh_addr + *(Elf32_Addr *)(si_->load_bias + shdrs_[SI_PLT].sh_addr + 16);
		DEBUG("kmpSearch �Ѿ��ҵ�.plt�� @offset: %X\n", shdrs_[SI_PLT].sh_offset);

		//�޸�.got, ͨ������.rel.dyn��������ΪR_ARM_GLOB_DAT�ķ�Χ��ȷ��, ��С������_GLOBAL_OFFSET_TABLE_�������ַȷ��
		//BUG:��С����ȷ, TODO
		{
			Elf32_Addr got_start = _GLOBAL_OFFSET_TABLE_;
			Elf32_Addr got_end = _GLOBAL_OFFSET_TABLE_ + 12 + 4 * (shdrs_[SI_RELPLT].sh_size / sizeof(Elf32_Rel));

			Elf32_Rel* rel = si_->rel;
			unsigned count = si_->rel_count;

			for (size_t idx = 0; idx < count; ++idx, ++rel)
			{
				unsigned type = ELF32_R_TYPE(rel->r_info);
				if (type == R_ARM_GLOB_DAT)
				{
					got_start = MIN(got_start, rel->r_offset);
				}
			}

			shdrs_[SI_GOT].sh_name = GetShdrName(SI_GOT);
			shdrs_[SI_GOT].sh_type = SHT_PROGBITS;
			shdrs_[SI_GOT].sh_flags = SHF_ALLOC | SHF_WRITE;
			shdrs_[SI_GOT].sh_addr = got_start;
			shdrs_[SI_GOT].sh_offset = addrToOff(shdrs_[SI_GOT].sh_addr);
			shdrs_[SI_GOT].sh_size = got_end - got_start;
			shdrs_[SI_GOT].sh_link = 0;
			shdrs_[SI_GOT].sh_info = 0;
			shdrs_[SI_GOT].sh_addralign = 4;
			shdrs_[SI_GOT].sh_entsize = 0;
		}
	}

	//�޸�.data.rel.ro��, ͨ������.rel.dyn��������ΪR_ARM_ABS32�ķ�Χ��ȷ��, ��ʼ��ַ-4����
	//BUG: ��Χ����ȷ, TODO
	{
		Elf32_Addr data_rel_ro_start = 0;
		Elf32_Addr data_rel_ro_end = 0;

		Elf32_Rel* rel = si_->rel;
		unsigned count = si_->rel_count;

		for (size_t idx = 0; idx < count; ++idx, ++rel)
		{
			unsigned type = ELF32_R_TYPE(rel->r_info);
			if (type == R_ARM_GLOB_DAT)
			{
				if (data_rel_ro_start == 0)
				{
					data_rel_ro_start = rel->r_offset - 4;
					data_rel_ro_end = rel->r_offset + 4;
				}
				else
				{
					data_rel_ro_start = MIN(data_rel_ro_start, rel->r_offset - 4);
					data_rel_ro_end = MAX(data_rel_ro_end, rel->r_offset + 4);
				}			
			}
		}

		if (data_rel_ro_start != 0)
		{
			shdrs_[SI_DATA_REL_RO].sh_name = GetShdrName(SI_DATA_REL_RO);
			shdrs_[SI_DATA_REL_RO].sh_type = SHT_PROGBITS;
			shdrs_[SI_DATA_REL_RO].sh_flags = SHF_ALLOC | SHF_WRITE;
			shdrs_[SI_DATA_REL_RO].sh_addr = data_rel_ro_start;
			shdrs_[SI_DATA_REL_RO].sh_offset = addrToOff(shdrs_[SI_DATA_REL_RO].sh_addr);
			shdrs_[SI_DATA_REL_RO].sh_size = data_rel_ro_end - data_rel_ro_start;
			shdrs_[SI_DATA_REL_RO].sh_link = 0;
			shdrs_[SI_DATA_REL_RO].sh_info = 0;
			shdrs_[SI_DATA_REL_RO].sh_addralign = 4;
			shdrs_[SI_DATA_REL_RO].sh_entsize = 0;
		}
	}

	//�޸�.text��, �򵥵Ľ�.plt֮��.ARM.exidx/.fini_array/.init_array/.data.rel.ro/.dynamic֮��Ľ���Ϊ.text, ֮�����ͨ��IDA����������޸�
	if(shdrs_[SI_PLT].sh_type)
	{
		Elf32_Shdr *start_shdr = &shdrs_[SI_PLT];
		Elf32_Shdr *end_shdr = nullptr;
		if (shdrs_[SI_ARMEXIDX].sh_type)
		{
			end_shdr = &shdrs_[SI_ARMEXIDX];
		}
		else if (shdrs_[SI_FINI_ARRAY].sh_type)
		{
			end_shdr = &shdrs_[SI_FINI_ARRAY];
		}
		else if (shdrs_[SI_INIT_ARRAY].sh_type)
		{
			end_shdr = &shdrs_[SI_INIT_ARRAY];
		}
		else if (shdrs_[SI_DATA_REL_RO].sh_type)
		{
			end_shdr = &shdrs_[SI_DATA_REL_RO];
		}
		else if (shdrs_[SI_DYNAMIC].sh_type)
		{
			end_shdr = &shdrs_[SI_DYNAMIC];
		}

		if (end_shdr && 
			(ALIGN(start_shdr->sh_addr + start_shdr->sh_size, end_shdr->sh_addralign) != end_shdr->sh_addr))
		{
			shdrs_[SI_TEXT].sh_name = GetShdrName(SI_TEXT);
			shdrs_[SI_TEXT].sh_type = SHT_PROGBITS;
			shdrs_[SI_TEXT].sh_flags = SHF_ALLOC | SHF_EXECINSTR;
			shdrs_[SI_TEXT].sh_addr = ALIGN(start_shdr->sh_addr + start_shdr->sh_size, 4);
			shdrs_[SI_TEXT].sh_offset = addrToOff(shdrs_[SI_TEXT].sh_addr);
			shdrs_[SI_TEXT].sh_size = end_shdr->sh_addr - shdrs_[SI_TEXT].sh_addr;
			shdrs_[SI_TEXT].sh_link = 0;
			shdrs_[SI_TEXT].sh_info = 0;
			shdrs_[SI_TEXT].sh_addralign = 4;
			shdrs_[SI_TEXT].sh_entsize = 0;
		}
	}

	//�޸�.rodata��, �򵥵Ľ�.text/.ARM.exidx֮���fini_array/.init_array/.data.rel.ro/.dynamic֮�����Ϊ.rodata
	//BUG: �ڵĴ�С���ܳ������ڶε�����ļ�ӳ��
	if (shdrs_[SI_TEXT].sh_type)
	{
		Elf32_Shdr *start_shdr = &shdrs_[SI_TEXT];
		Elf32_Shdr *end_shdr = nullptr;

		if (shdrs_[SI_ARMEXIDX].sh_type)
		{
			start_shdr = &shdrs_[SI_ARMEXIDX];
		}
	
		if (shdrs_[SI_FINI_ARRAY].sh_type)
		{
			end_shdr = &shdrs_[SI_FINI_ARRAY];
		}
		else if (shdrs_[SI_INIT_ARRAY].sh_type)
		{
			end_shdr = &shdrs_[SI_INIT_ARRAY];
		}
		else if (shdrs_[SI_DATA_REL_RO].sh_type)
		{
			end_shdr = &shdrs_[SI_DATA_REL_RO];
		}
		else if (shdrs_[SI_DYNAMIC].sh_type)
		{
			end_shdr = &shdrs_[SI_DYNAMIC];
		}
		
		if (end_shdr &&
			(ALIGN(start_shdr->sh_addr + start_shdr->sh_size, end_shdr->sh_addralign) != end_shdr->sh_addr))
		{
			shdrs_[SI_RODATA].sh_name = GetShdrName(SI_RODATA);
			shdrs_[SI_RODATA].sh_type = SHT_PROGBITS;
			shdrs_[SI_RODATA].sh_flags = SHF_ALLOC;
			shdrs_[SI_RODATA].sh_addr = ALIGN(start_shdr->sh_addr + start_shdr->sh_size, 16);
			shdrs_[SI_RODATA].sh_offset = addrToOff(shdrs_[SI_RODATA].sh_addr);
			shdrs_[SI_RODATA].sh_size = end_shdr->sh_addr - shdrs_[SI_RODATA].sh_addr;
			shdrs_[SI_RODATA].sh_link = 0;
			shdrs_[SI_RODATA].sh_info = 0;
			shdrs_[SI_RODATA].sh_addralign = 16;
			shdrs_[SI_RODATA].sh_entsize = 0;
		}
	}

	//�޸�.data��, �򵥵Ľ�.got֮�󵽿ɼ��ضε�ʵ���ļ�ӳ�䷶Χ��Ϊ.data��
	if (shdrs_[SI_GOT].sh_type)
	{
		Elf32_Shdr *start_shdr = &shdrs_[SI_GOT];
		Elf32_Addr end_addr = 0;

		const Elf32_Phdr* phdr = si_->phdr;
		const Elf32_Phdr* phdr_limit = phdr + si_->phnum;

		for (phdr = si_->phdr; phdr < phdr_limit; phdr++)
		{
			if (phdr->p_type != PT_LOAD)
			{
				continue;
			}

			end_addr = MAX(end_addr, phdr->p_paddr + phdr->p_filesz);
		}

		shdrs_[SI_DATA].sh_name = GetShdrName(SI_DATA);
		shdrs_[SI_DATA].sh_type = SHT_PROGBITS;
		shdrs_[SI_DATA].sh_flags = SHF_ALLOC | SHF_WRITE;
		shdrs_[SI_DATA].sh_addr = ALIGN(start_shdr->sh_addr + start_shdr->sh_size, 4);
		shdrs_[SI_DATA].sh_offset = addrToOff(shdrs_[SI_DATA].sh_addr);
		shdrs_[SI_DATA].sh_size = end_addr - shdrs_[SI_DATA].sh_addr;
		shdrs_[SI_DATA].sh_link = 0;
		shdrs_[SI_DATA].sh_info = 0;
		shdrs_[SI_DATA].sh_addralign = 4;
		shdrs_[SI_DATA].sh_entsize = 0;
	}

	//�޸�.bss��, �򵥵Ľ�.data��֮�󵽿ɼ��ضε�ʵ���ڴ��ַ��Ϊ.bss��
	if (shdrs_[SI_DATA].sh_type)
	{
		Elf32_Shdr *start_shdr = &shdrs_[SI_DATA];
		Elf32_Addr end_addr = 0;

		const Elf32_Phdr* phdr = si_->phdr;
		const Elf32_Phdr* phdr_limit = phdr + si_->phnum;

		for (phdr = si_->phdr; phdr < phdr_limit; phdr++)
		{
			if (phdr->p_type != PT_LOAD)
			{
				continue;
			}

			end_addr = MAX(end_addr, phdr->p_paddr + phdr->p_memsz);
		}

		shdrs_[SI_BSS].sh_name = GetShdrName(SI_BSS);
		shdrs_[SI_BSS].sh_type = SHT_NOBITS;
		shdrs_[SI_BSS].sh_flags = SHF_ALLOC | SHF_WRITE;
		shdrs_[SI_BSS].sh_addr = ALIGN(start_shdr->sh_addr + start_shdr->sh_size, 16);
		shdrs_[SI_BSS].sh_offset = addrToOff(shdrs_[SI_DATA].sh_addr);
		shdrs_[SI_BSS].sh_size = end_addr - shdrs_[SI_DATA].sh_addr;
		shdrs_[SI_BSS].sh_link = 0;
		shdrs_[SI_BSS].sh_info = 0;
		shdrs_[SI_BSS].sh_addralign = 16;
		shdrs_[SI_BSS].sh_entsize = 0;
	}

	return true;
}

Elf32_Off ElfFixer::addrToOff(Elf32_Addr addr)
{
	const Elf32_Phdr* phdr = si_->phdr;
	const Elf32_Phdr* phdr_limit = phdr + si_->phnum;
	Elf32_Off off = -1;

	for (phdr = si_->phdr; phdr < phdr_limit; phdr++)
	{
		if (phdr->p_type != PT_LOAD)
		{
			continue;
		}

		Elf32_Addr seg_start = phdr->p_vaddr;	//�ڴ�ӳ��ʵ����ʼ��ַ
		Elf32_Addr seg_end = seg_start + phdr->p_memsz;	//�ڴ�ӳ��ʵ����ֹ��ַ

		Elf32_Addr seg_page_start = PAGE_START(seg_start); //�ڴ�ӳ��ҳ�׵�ַ
		Elf32_Addr seg_page_end = PAGE_END(seg_end);	   //�ڴ�ӳ����ֹҳ

		Elf32_Addr seg_file_end = seg_start + phdr->p_filesz; //�ļ�ӳ����ֹҳ

		// File offsets.
		Elf32_Addr file_start = phdr->p_offset;
		Elf32_Addr file_end = file_start + phdr->p_filesz;

		Elf32_Addr file_page_start = PAGE_START(file_start); //�ļ�ӳ��ҳ�׵�ַ
		Elf32_Addr file_length = file_end - file_page_start; //�ļ�ӳ���С

		//���û�п�дȨ��, �����ļ���ʵ��ӳ���С
		if ((phdr->p_flags & PF_W) == 0)
		{
			seg_file_end = PAGE_END(seg_file_end);
		}

		//�ж�addr�Ƿ���LOAD����
		if (addr >= seg_page_start && addr < seg_file_end)
		{
			off = file_page_start + addr - seg_page_start;
			break;
		}
	}

	return off;
}

int ElfFixer::findShIdx(Elf32_Off off)
{
	int idx = -1;
	for (int i = 0; i < SI_MAX; i++)
	{
		if (off >= shdrs_[i].sh_offset && off < shdrs_[i].sh_offset + shdrs_[i].sh_size)
		{
			idx = i;
			break;
		}
	}

	return idx;
}