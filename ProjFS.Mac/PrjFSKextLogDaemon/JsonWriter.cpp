#include "JsonWriter.hpp"

using std::string;

JsonWriter::JsonWriter()
    : jsonBuffer("{")
{
}

JsonWriter::~JsonWriter()
{
}

void JsonWriter::Add(const string& key, const JsonWriter& value)
{
    this->AddCommaIfNeeded();
    this->AddKey(key);
    this->jsonBuffer += "\"" + value.ToString() + "\"";
}

void JsonWriter::Add(const string& key, const string& value)
{
    this->AddCommaIfNeeded();
    this->AddKey(key);
    this->AddStringValue(value);
}

void JsonWriter::Add(const std::string& key, int32_t value)
{
    this->AddUnquoted(key, value);
}

void JsonWriter::Add(const string& key, uint32_t value)
{
    this->AddUnquoted(key, value);
}

void JsonWriter::Add(const string& key, uint64_t value)
{
    this->AddUnquoted(key, value);
}

string JsonWriter::ToString() const
{
    return this->jsonBuffer + "}";
}

void JsonWriter::AddCommaIfNeeded()
{
    if ("{" != this->jsonBuffer)
    {
        this->jsonBuffer += ",";
    }
}

void JsonWriter::AddKey(const string& key)
{
    this->jsonBuffer += "\"" + key + "\":";
}


void JsonWriter::AddStringValue(const string& value)
{
    // TODO: Escape characters properly
    this->jsonBuffer += "\"" + value + "\"";
}
