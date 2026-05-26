#include "arg_parser.h"

#include <cstdlib>
#include <iomanip>

Argument::Argument(const std::string& longName, const std::string& shortName) :
    m_longName(longName),
    m_shortName(shortName) {}

Argument& Argument::help(const std::string& text)
{
    m_description = text;
    return *this;
}

Argument& Argument::defaultValue(const std::string& value)
{
    m_defaultValue = value;
    m_hasDefaultValue = true;
    return *this;
}

Argument& Argument::required()
{
    m_isRequired = true;
    return *this;
}

Argument& Argument::actionFlag()
{
    m_isFlag = true;
    return *this;
}

std::string Argument::getCleanName() const
{
    std::string clean = m_longName;
    size_t start = clean.find_first_not_of("-");
    if (start != std::string::npos)
        clean = clean.substr(start);

    return clean;
}

ArgParser::ArgParser(const std::string& programName, const std::string& description) :
    m_programName(programName),
    m_description(description)
{
    addArgument("--help", "-h").help("print help message and exit.").actionFlag();
}

Argument& ArgParser::addArgument(const std::string& longName, const std::string& shortName)
{
    auto arg = std::make_unique<Argument>(longName, shortName);
    Argument* rawPtr = arg.get();
    
    m_argumentMap[longName] = rawPtr;
    if (!shortName.empty())
        m_argumentMap[shortName] = rawPtr;
    
    m_arguments.push_back(std::move(arg));
    return *rawPtr;
}

void ArgParser::parseArgs(int argc, const char* argv[])
{
    for (int i = 1; i < argc; ++i) {
        std::string currentArg = argv[i];

        auto it = m_argumentMap.find(currentArg);
        if (it != m_argumentMap.end()) {
            Argument* arg = it->second;
            std::string cleanName = arg->getCleanName();

            if (arg->isFlag()) {
                m_parsedValues[cleanName] = "true";
                if (cleanName == "help") {
                    printHelp();
                    exit(0);
                }
            } else {
                if (i + 1 < argc && std::string(argv[i + 1]).find("-") != 0) {
                    m_parsedValues[cleanName] = argv[++i];
                } else {
                    throw std::runtime_error("error: '" + currentArg + "' requires a value.");
                }
            }
        } else {
            throw std::runtime_error("error: unknown argument '" + currentArg + "'");
        }
    }

    for (const auto& arg : m_arguments) {
        std::string cleanName = arg->getCleanName();
        if (arg->isRequired() && m_parsedValues.find(cleanName) == m_parsedValues.end()) {
            throw std::runtime_error("error: required argument is missing ('" + arg->getLongName() + "')");
        }
    }
}

std::string ArgParser::getRawValue(const std::string& name) const
{
    auto it = m_parsedValues.find(name);
    if (it != m_parsedValues.end()) {
        return it->second;
    }

    for (const auto& arg : m_arguments) {
        if (arg->getCleanName() == name) {
            if (arg->hasDefaultValue()) {
                return arg->getDefaultValue();
            }
            break;
        }
    }
    
    throw std::invalid_argument("error: requested argument not found or not set: " + name);
}

bool ArgParser::has(const std::string& name) const
{
    return m_parsedValues.find(name) != m_parsedValues.end();
}

void ArgParser::printHelp() const
{
    std::cout << "Usage: " << m_programName << " [options...]\n";
    if (!m_description.empty()) {
        std::cout << m_description << "\n";
    }
    std::cout << "\nOptions:\n";

    for (const auto& arg : m_arguments) {
        std::string nameStr = arg->getLongName();
        if (!arg->getShortName().empty()) {
            nameStr += ", " + arg->getShortName();
        }
        
        std::cout << "  " << std::left << std::setw(20) << nameStr << " " << arg->getDescription();
        if (arg->isRequired()) {
            std::cout << " (required)";
        } else if (arg->hasDefaultValue() && !arg->getDefaultValue().empty()) {
            std::cout << " (default: " << arg->getDefaultValue() << ")";
        }
        std::cout << "\n";
    }
}