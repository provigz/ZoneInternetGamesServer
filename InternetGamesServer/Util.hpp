#pragma once

#include <string>
#include <cassert>
#include <fstream>
#include <random>
#include <vector>

#include <winsock2.h>

#include "tinyxml2.h"

/** Debug */
#define LOG_DEBUG 0 // DEBUG: Log certain operations to console

/** Logging */
void SetSessionLog(std::unique_ptr<std::ofstream> stream);
std::ostream& SessionLog();
void FlushSessionLog();

/** String utilities */
bool StartsWith(const std::string& str, const std::string& prefix);
std::vector<std::string> StringSplit(std::string str, const std::string& delimiter);
void RemoveNewlines(std::string& str);

/** Encoding/Decoding */
std::string DecodeURL(const std::string& str);

/* I/O */
void CreateNestedDirectories(const std::string& path);

class NullStream final : public std::ostream
{
public:
	NullStream();

private:
	class NullBuffer : public std::streambuf
	{
	protected:
		int overflow(int c) override { return c; }
	};

private:
	NullBuffer m_buffer;
};

/** TinyXML2 */
class XMLPrinter final : private tinyxml2::XMLPrinter
{
private:
	std::vector<const char*> m_elementTree;

public:
	XMLPrinter();

	void OpenElement(const char* name);
	inline void OpenElement(const std::string& name) { OpenElement(name.c_str()); }
	void CloseElement(const char* name);
	inline void CloseElement(const std::string& name) { CloseElement(name.c_str()); }

	inline void PushText(const std::string& text)
	{
		tinyxml2::XMLPrinter::PushText(text.c_str());
	}
	inline void PushComment(const std::string& comment)
	{
		tinyxml2::XMLPrinter::PushComment(comment.c_str());
	}

	std::string print() const;

	inline operator std::string() const { return print(); }
};

tinyxml2::XMLElement* NewElementWithText(tinyxml2::XMLElement* root, const std::string& name, std::string text);
inline tinyxml2::XMLElement* NewElementWithText(tinyxml2::XMLElement* root, const std::string& name, const char* text)
{
	return NewElementWithText(root, name, std::string(text));
};
template<typename T>
inline tinyxml2::XMLElement* NewElementWithText(tinyxml2::XMLElement* root, const std::string& name, T val)
{
	tinyxml2::XMLElement* el = root->GetDocument()->NewElement(name.c_str());
	el->SetText(val);
	root->InsertEndChild(el);
	return el;
};

void NewElementWithText(XMLPrinter& printer, const std::string& name, std::string text);
inline void NewElementWithText(XMLPrinter& printer, const std::string& name, const char* text)
{
	NewElementWithText(printer, name, std::string(text));
};
template<typename T>
inline void NewElementWithText(XMLPrinter& printer, const std::string& name, T val)
{
	const std::string text = std::to_string(val);

	printer.OpenElement(name);
	printer.PushText(text.c_str());
	printer.CloseElement(name);
};

/** Random generation */
extern std::mt19937 g_rng;

std::vector<int> GenerateUniqueRandomNums(int start, int end);

/** Printing */
std::ostream& operator<<(std::ostream& os, REFGUID guid);

/* Exception handling */
class MutexError final : public std::exception
{
public:
	MutexError(const std::string& err) :
		m_err(err)
	{}

	const char* what() const override { return m_err.c_str(); }

private:
	const std::string m_err;
};

/** Other utility classes */
template<typename T, size_t Size>
struct Array final
{
	T raw[Size] = {};

	constexpr size_t GetSize() { return Size; }

	inline size_t GetLength() const { return len; }
	inline void SetLength(size_t newLen)
	{
		assert(newLen <= Size);
		len = newLen;
	}

	inline T& operator[](int idx) { return raw[idx]; }
	inline const T& operator[](int idx) const { return raw[idx]; }

	class iterator final
	{
	public:
		iterator(Array<T, Size>& parent, size_t index = 0) :
			el(parent.raw + index)
		{}

		inline T& operator*() { return *el; }
		inline iterator& operator++()
		{
			++el;
			return *this;
		}

		inline bool operator==(const iterator& other) const { return el == other.el; }
		inline bool operator!=(const iterator& other) const { return el != other.el; }

	private:
		T* el;
	};
	class const_iterator final
	{
	public:
		const_iterator(const Array<T, Size>& parent, size_t index = 0) :
			el(parent.raw + index)
		{}

		inline const T& operator*() const { return *el; }
		inline const_iterator& operator++()
		{
			++el;
			return *this;
		}

		inline bool operator==(const const_iterator& other) const { return el == other.el; }
		inline bool operator!=(const const_iterator& other) const { return el != other.el; }

	private:
		const T* el;
	};

	inline iterator begin() { return iterator(*this); }
	inline iterator end() { return iterator(*this, len - 1); }

	inline const_iterator begin() const { return const_iterator(*this); }
	inline const_iterator end() const { return const_iterator(*this, len - 1); }

private:
	size_t len = 0;
};
template<typename T, size_t Size>
std::ostream& operator<<(std::ostream& os, const Array<T, Size>& arr)
{
	os << '[';
	if (arr.GetLength() <= 0)
		return os;

	os << arr.raw[0];
	for (size_t i = 1; i < arr.GetLength(); ++i)
		os << ", " << arr.raw[i];

	return os << ']';
}

template<typename T>
class ChangeTimeTracker final
{
public:
	ChangeTimeTracker() :
		val(),
		timeLastChange(GetTickCount())
	{}
	ChangeTimeTracker(T defaultVal) :
		val(std::move(defaultVal)),
		timeLastChange(GetTickCount())
	{}

	inline const T& Get() const { return val; }
	inline operator const T&() const { return val; }

	inline DWORD GetTimeLastChange() const { return timeLastChange; }
	inline DWORD GetTimePassedMS() const { return GetTickCount() - timeLastChange; }

	void operator=(const T& otherVal)
	{
		if (val == otherVal)
			return;

		val = otherVal;
		timeLastChange = GetTickCount();
	}

private:
	T val;
	DWORD timeLastChange;
};

/** Struct padding */
#define STRUCT_PADDING(x) private: char _struct_padding_bytes[x];

template<typename T>
struct HasPaddingSize final
{
private:
	template<typename U>
	static auto test(int) -> decltype(U::_struct_padding_bytes, std::true_type()) {}

	template<typename>
	static std::false_type test(...) {}

public:
	static constexpr bool value = decltype(test<T>(0))::value;
};
template<typename T, bool HasPadding = HasPaddingSize<T>::value>
struct AdjustedSizeHelper final
{
	static constexpr size_t value = sizeof(T);
};
template<typename T>
struct AdjustedSizeHelper<T, true> final
{
	static constexpr size_t value = sizeof(T) - sizeof(T::_struct_padding_bytes);
};

template<typename T>
static constexpr size_t AdjustedSize = AdjustedSizeHelper<T>::value;

/** Macros */
#define ROUND_DATA_LENGTH_UINT32(len) ((len + 3) & ~0x3)

#define ARRAY_EACH_TRUE(arr) std::all_of(arr.begin(), arr.end(), [](bool b) { return b; })
#define ARRAY_EACH_EQUALS(arr, val) std::all_of(arr.begin(), arr.end(), [](auto b) { return b == val; })
