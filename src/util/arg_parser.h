#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <stdexcept>
#include <sstream>
#include <iostream>

class Argument
{
public:
    Argument(const std::string& longName, const std::string& shortName = "");

    Argument& help(const std::string& text);
    Argument& defaultValue(const std::string& value);
    Argument& required();
    Argument& actionFlag();

    std::string getLongName() const { return m_longName; }
    std::string getShortName() const { return m_shortName; }
    std::string getDescription() const { return m_description; }
    std::string getDefaultValue() const { return m_defaultValue; }
    bool isRequired() const { return m_isRequired; }
    bool isFlag() const { return m_isFlag; }

    std::string getCleanName() const;

private:
    std::string m_longName;
    std::string m_shortName;
    std::string m_description;
    std::string m_defaultValue;
    bool m_isRequired = false;
    bool m_isFlag = false;
};

class ArgParser
{
public:
    ArgParser(const std::string& programName, const std::string& description = "");

    Argument& addArgument(const std::string& longName, const std::string& shortName = "");

    void parseArgs(int argc, const char* argv[]);
    void printHelp() const;
    bool has(const std::string& name) const;

    template <typename T>
    T get(const std::string& name) const
    {
        std::string strVal = getRawValue(name);
        std::istringstream iss(strVal);
        T result;
        iss >> result;
        return result;
    }

private:
    std::string m_programName;
    std::string m_description;
    
    std::vector<std::unique_ptr<Argument>> m_arguments;
    std::unordered_map<std::string, Argument*> m_argumentMap;
    std::unordered_map<std::string, std::string> m_parsedValues;

    std::string getRawValue(const std::string& name) const;
};

template<>
inline std::string ArgParser::get<std::string>(const std::string& name) const
{
    return getRawValue(name);
}