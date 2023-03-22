//
//  riffrw.h
//  generic implementation
//
//  created by yu2924 on 2023-03-19
//

#pragma once

#include <filesystem>
#include <fstream>
#include <vector>
#include <list>
#include <functional>

namespace riffrw
{

	struct ChunkHeader
	{
		uint32_t ckid;
		uint32_t cksize;
		bool isContainer() const
		{
			return (ckid == *(uint32_t*)"RIFF") || (ckid == *(uint32_t*)"LIST");
		}
	};

	struct ChunkInfo
	{
		uint32_t hdroffset;
		ChunkHeader header;
		uint32_t type;
		std::string pathElement() const
		{
			std::string s = std::string((const char*)&header.ckid, 4);
			if(header.isContainer()) s += std::string(".") + std::string((const char*)&type, 4);
			return s;
		}
	};

	class RiffReader
	{
	public:
		std::istream& stream;
		std::list<ChunkInfo> ckstack;
		RiffReader(std::istream& str) : stream(str)
		{
		}
		operator bool() const
		{
			return stream.good();
		}
		bool canDescend() const
		{
			if(ckstack.empty()) return true;
			const ChunkInfo& ck = ckstack.back();
			if(!ck.header.isContainer()) return false;
			uint32_t startpos = ck.hdroffset + 12;
			uint32_t endpos = ck.hdroffset + 8 + ck.header.cksize;
			uint32_t pos = (uint32_t)stream.tellg();
			return (startpos <= pos) && ((pos + 8) <= endpos);
		}
		bool descend(ChunkInfo* pck)
		{
			ChunkInfo ck = {};
			ck.hdroffset = (uint32_t)stream.tellg();
			stream.read((char*)&ck.header, 8);
			if(ck.header.isContainer()) stream.read((char*)&ck.type, 4);
			ckstack.push_back(ck);
			*pck = ck;
			return stream.good();
		}
		bool ascend()
		{
			if(ckstack.empty()) return false;
			ChunkInfo& ck = ckstack.back();
			uint32_t endpos = (ck.hdroffset + 8 + ck.header.cksize + 1) & ~0x01;
			stream.seekg(endpos);
			ckstack.pop_back();
			return stream.good();
		}
		int read(void* p, size_t c)
		{
			stream.read((char*)p, c);
			return (int)stream.gcount();
		}
	};

	class RiffWriter
	{
	public:
		std::ostream& stream;
		std::list<ChunkInfo> ckstack;
		RiffWriter(std::ostream& str) : stream(str)
		{
		}
		~RiffWriter()
		{
			while(!ckstack.empty()) ascend();
		}
		operator bool() const
		{
			return stream.good();
		}
		bool descend(const char* ckid, const char* type_or_z = nullptr)
		{
			return descend(*(uint32_t*)ckid, type_or_z ? *(uint32_t*)type_or_z : 0);
		}
		bool descend(uint32_t ckid, uint32_t type_or_z = 0)
		{
			ChunkInfo ck = {};
			ck.hdroffset = (uint32_t)stream.tellp();
			ck.header.ckid = ckid;
			ckstack.push_back(ck);
			stream.write((const char*)&ck.header, 8);
			if(ck.header.isContainer()) stream.write((const char*)&type_or_z, 4);
			return stream.good();
		}
		bool ascend()
		{
			if(ckstack.empty()) return false;
			ChunkInfo& ck = ckstack.back();
			uint32_t endpos = (uint32_t)stream.tellp();
			ck.header.cksize = endpos - ck.hdroffset - 8;
			stream.seekp(ck.hdroffset + 4);
			stream.write((const char*)&ck.header.cksize, 4);
			stream.seekp(endpos);
			if(endpos & 0x01) stream.put(0);
			ckstack.pop_back();
			return stream.good();
		}
		bool write(const void* p, size_t c)
		{
			stream.write((const char*)p, c);
			return stream.good();
		}
		struct ScopedDescend
		{
			RiffWriter& writer;
			ScopedDescend(RiffWriter& w, uint32_t ckid, uint32_t type_or_z = 0) : writer(w) { writer.descend(ckid, type_or_z); }
			ScopedDescend(RiffWriter& w, const char* ckid, const char* type_or_z = nullptr) : writer(w) { writer.descend(ckid, type_or_z); }
			~ScopedDescend() { writer.ascend(); }
		};
	};

	struct RiffNode
	{
		ChunkInfo ckinfo = {};
		RiffNode* parent = 0;
		std::list<RiffNode> subnodes;
		std::shared_ptr<std::vector<char> > replacingdata;
		RiffNode() = default;
		RiffNode(uint32_t uckid, uint32_t utype = 0)
		{
			ckinfo.header.ckid = uckid;
			ckinfo.type = utype;
		}
		RiffNode(const char* sckid, const char* stype = nullptr)
		{
			ckinfo.header.ckid = *(uint32_t*)sckid;
			if(stype) ckinfo.type = *(uint32_t*)stype;
		}
		std::string nodePath() const
		{
			std::list<std::string> elist;
			for(const RiffNode* pn = this; pn; pn = pn->parent) elist.push_front(pn->ckinfo.pathElement());
			std::string path;
			for(const auto& e : elist) path += "/" + e;
			return path;
		}
		RiffNode& addSubNode(const RiffNode& nsadd)
		{
			subnodes.push_back(nsadd);
			RiffNode& nsnew = subnodes.back();
			nsnew.parent = this;
			return nsnew;
		}
		static bool readTree(RiffReader& reader, RiffNode* pn)
		{
			if(!reader.descend(&pn->ckinfo)) return false;
			if(pn->ckinfo.header.isContainer())
			{
				while(reader.canDescend())
				{
					RiffNode& ns = pn->addSubNode({});
					if(!readTree(reader, &ns)) return false;
				}
			}
			if(!reader.ascend()) return false;
			return true;
		}
		static bool writeTree(const RiffNode& n, std::istream& istr, RiffWriter& writer)
		{
			if(!writer.descend(n.ckinfo.header.ckid, n.ckinfo.type)) return false;
			if(n.ckinfo.header.isContainer())
			{
				for(const auto& ns : n.subnodes)
				{
					if(!writeTree(ns, istr, writer)) return false;
				}
			}
			else
			{
				if(n.replacingdata)
				{
					if(!writer.write(n.replacingdata->data(), n.replacingdata->size())) return false;
				}
				else
				{
					istr.seekg(n.ckinfo.hdroffset + 8);
					std::vector<char> buf(1024);
					size_t c = n.ckinfo.header.cksize, i = 0;
					while(i < c)
					{
						size_t lseg = std::min(c - i, buf.size());
						istr.read(buf.data(), lseg);
						if(!writer.write(buf.data(), lseg)) return false;
						i += lseg;
					}
				}
			}
			if(!writer.ascend()) return false;
			return true;
		}
		static bool readTreeFromStream(std::istream& istr, RiffNode* pn)
		{
			RiffReader reader(istr);
			return readTree(reader, pn);
		}
		static bool readTreeFromFile(const std::filesystem::path& path, RiffNode* pn)
		{
			std::fstream istr(path, std::ios::in | std::ios::binary);
			if(!istr.good()) return false;
			return readTreeFromStream(istr, pn);
		}
		static bool writeTreeToStream(const RiffNode& n, std::istream& istr, std::ostream& ostr)
		{
			RiffWriter writer(ostr);
			return writeTree(n, istr, writer);
		}
		static bool writeTreeToFile(const RiffNode& n, const std::filesystem::path& inpath, const std::filesystem::path& outpath)
		{
			std::fstream istr(inpath, std::ios::in | std::ios::binary);
			if(!istr.good()) return false;
			std::fstream ostr(outpath, std::ios::out | std::ios::binary | std::ios::trunc);
			if(!ostr.good()) return false;
			return writeTreeToStream(n, istr, ostr);
		}
		static void traverseTree(RiffNode& n, bool& continueFlag, std::function<void(RiffNode&, bool&)> callback)
		{
			if(callback) callback(n, continueFlag);
			if(!continueFlag) return;
			if(n.ckinfo.header.isContainer())
			{
				for(auto&& ns : n.subnodes)
				{
					traverseTree(ns, continueFlag, callback);
					if(!continueFlag) return;
				}
			}
		}
		static void traverseTree(RiffNode& n, std::function<void(RiffNode&)> callback)
		{
			bool cflg = true;
			traverseTree(n, cflg, [callback](RiffNode& n, bool&) { callback(n); });
		}
		static RiffNode* findNode(RiffNode& n, const std::string& path)
		{
			bool continueFlag = true; RiffNode* pn = nullptr;
			traverseTree(n, continueFlag, [path, &pn](RiffNode& en, bool& continueFlag)
			{
				if(en.nodePath() == path) { pn = &en; continueFlag = false; }
			});
			return pn;
		}
	};

} // namespace riffrw
