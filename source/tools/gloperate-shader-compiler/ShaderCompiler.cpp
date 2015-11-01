#include "ShaderCompiler.h"

#include <cassert>

#include <iostream>
#include <vector>
#include <algorithm>

#include <QDebug>
#include <QJsonArray>
#include <QJsonObject>

#include <iozeug/filename.h>
#include <iozeug/directorytraversal.h>

#include <glbinding/gl/functions.h>

#include <globjects/globjects.h>
#include <globjects/base/File.h>
#include <globjects/NamedString.h>
#include <globjects/Program.h>
#include <globjects/Shader.h>
#include <globjects/logging.h>
#include <globjects/base/File.h>
#include <globjects/base/StringTemplate.h>

#include "OpenGLContext.h"


bool ShaderCompiler::process(const QJsonObject & config)
{
    return ShaderCompiler{}.parse(config);
}

bool ShaderCompiler::parse(const QJsonObject & config)
{   
    const auto jsonOpenGLConfig = config.value("opengl");
    
    if (!jsonOpenGLConfig.isObject())
    {
        error(JsonParseError::PropertyNotFoundOrNotAnObject, "opengl");
        return false;
    }
    
    auto parseError = JsonParseError{};
    auto context = OpenGLContext::fromJsonConfig(jsonOpenGLConfig.toObject(), &parseError);
    
    if (parseError)
    {
        error(parseError);
        return false;
    }
    
    if (!context.create())
    {
        error(JsonParseError::ContextCreationFailed);
        return false;
    }
    
    if (!context.makeCurrent())
    {
        error(JsonParseError::ContextActivationFailed);
        return false;
    }
    
    globjects::init();
    
    info(Info::Driver);
    
    const auto jsonNamedStringPaths = config.value("namedStringPaths");
    
    if (jsonNamedStringPaths.isArray())
    {
        if (!parseNamedStringPaths(jsonNamedStringPaths.toArray()))
            return false;
    }
    
    const auto jsonPrograms = config.value("programs");
    
    if (!jsonPrograms.isArray())
    {
        error(JsonParseError::ArrayNotFoundOrEmpty, "programs");
        return false;
    }
    
    auto ok = parsePrograms(jsonPrograms.toArray());
    
    context.doneCurrent();
    
    info(Info::Failures);
    
    return ok;
}

bool ShaderCompiler::parseNamedStringPaths(const QJsonArray & paths)
{
    bool ok{};
    
    std::vector<std::string> namedStrings;
    
    for (const auto & namedStringPath : paths)
    {
        if (!namedStringPath.isObject())
        {
            error({ JsonParseError::ElementNotObject, "namedStringPaths" });
            return false;
        }
        
        const auto pathObject = namedStringPath.toObject();
        
        const auto pathString = pathObject.value("path").toString();
        
        if (pathString.isNull())
        {
            error({ JsonParseError::PropertyNotFoundOrWrongFormat, "path" });
            return false;
        }
        
        const auto extensionsArray = pathObject.value("extensions").toArray();
        
        if (extensionsArray.isEmpty())
        {   
            error({ JsonParseError::ArrayNotFoundOrEmpty, "extensions" });
            return false;
        }
        
        const auto extensions = parseExtensions(extensionsArray, ok);
        
        if (!ok)
        {
            error({ JsonParseError::ElementWrongFormat, "extensions" });
            return false;
        }
        
        auto files = scanDirectory(pathString.toStdString(), extensions);
        
        if (files.empty())
        {
            error({ JsonParseError::NoFilesWithExtensionFound, pathString });
            return false;
        }
        
        const auto aliasString = pathObject.value("alias").toString();
        
        if (aliasString.isNull())
        {
            createNamedStrings(files, files);
            
            std::copy(files.begin(), files.end(), std::back_inserter(namedStrings));
        }
        else
        {
            const auto aliases = createAliases(files,
                pathString.toStdString(),
                aliasString.toStdString());
            
            std::copy(aliases.begin(), aliases.end(), std::back_inserter(namedStrings));
            
            createNamedStrings(files, aliases);
        }
    }
    
    if (!namedStrings.empty())
    {
        qDebug() << "Registered Named Strings:";
        for (const auto & namedString : namedStrings)
            qDebug().nospace() << "    " << QString::fromStdString(namedString);
    }

    return true;
}

std::set<std::string> ShaderCompiler::parseExtensions(
    const QJsonArray & extensionsArray,
    bool & ok)
{
    auto extensions = std::set<std::string>{};
    
    for (const auto & extensionValue : extensionsArray)
    {
        if (!extensionValue.isString())
        {
            ok = false;
            return extensions;
        }
        
        extensions.insert(extensionValue.toString().toStdString());
    }
    
    ok = true;
    return extensions;
}

std::vector<std::string> ShaderCompiler::scanDirectory(
    const std::string & path,
    const std::set<std::string> & extensions)
{
    auto files = std::vector<std::string>{};
    
    iozeug::scanDirectory(path, "*", true,
        [&extensions, &files] (const std::string & fileName)
        {
          const auto fileExtension = iozeug::getExtension(fileName);
          
          if (!extensions.count(fileExtension))
              return;
          
          files.push_back(fileName);
        });

    return files;
}

std::vector<std::string> ShaderCompiler::createAliases(
    const std::vector<std::string> & files,
    const std::string & path,
    const std::string & alias)
{
    std::vector<std::string> aliasedFiles{};

    for (const auto & file : files)
    {
        auto aliasedFile = alias;
        std::copy(file.begin() + path.size(), file.end(), std::back_inserter(aliasedFile));
        aliasedFiles.push_back(aliasedFile);
    }

    return aliasedFiles;
}

void ShaderCompiler::createNamedStrings(
    const std::vector<std::string> & files,
    const std::vector<std::string> & aliases)
{
    assert(files.size() == aliases.size());
    
    for (auto i = 0u; i < files.size(); ++i)
    {
        const auto file = files[i];
        const auto alias = aliases[i];
        
        const auto fileObject = new globjects::File(file);
        
        globjects::NamedString::create(alias, fileObject);
    }
}

bool ShaderCompiler::parsePrograms(const QJsonArray & programs)
{
    bool ok{};

    for (const auto programValue : programs)
    {
        if (!programValue.isObject())
        {
            error(JsonParseError::ElementNotObject, "programs");
            return false;
        }
        
        const auto programObject = programValue.toObject();
        
        const auto name = programObject.value("name").toString();
        
        if (name.isNull())
        {
            error(JsonParseError::PropertyNotFoundOrWrongFormat, "name");
            return false;
        }
        
        qDebug() << "";
        qDebug().noquote() << "Process" << name;
        
        const auto shadersArray = programObject.value("shaders");
        
        if (!shadersArray.isArray())
        {
            error(JsonParseError::ArrayNotFoundOrEmpty, "shaders");
            return false;
        }
        
        const auto shaders = parseShaders(shadersArray.toArray(), ok);
        
        if (!ok)
        {
            m_linkFailures.push_back(name.toStdString());
            continue;
        }
        
        qDebug().noquote() << "Link" << name;
        
        ok = createAndLinkProgram(shaders);
        
        if (!ok)
            m_linkFailures.push_back(name.toStdString());
    }
    
    return true;
}

std::vector<globjects::ref_ptr<globjects::Shader>> ShaderCompiler::parseShaders(
    const QJsonArray & shadersArray,
    bool & ok)
{
    std::vector<globjects::ref_ptr<globjects::Shader>> shaders{};
    
    for (const auto & shaderValue : shadersArray)
    {
        if (!shaderValue.isObject())
        {
            error(JsonParseError::ElementNotObject, "shaders");
            ok = false;
            return shaders;
        }
        
        const auto shaderObject = shaderValue.toObject();

        const auto fileName = shaderObject.value("file").toString();

        if (fileName.isNull())
        {
            error(JsonParseError::PropertyNotFoundOrWrongFormat, "file");
            ok = false;
            return shaders;
        }
        
        const auto name = shaderObject.value("name").toString();
        
        if (name.isNull())
            qDebug().noquote() << QString{"Compile %1"}.arg(fileName);
        else
            qDebug().noquote() << QString{"Compile %1 ('%2')"}.arg(name).arg(fileName);

        const auto typeString = shaderObject.value("type").toString();
        
        if (typeString.isNull())
        {
            error(JsonParseError::PropertyNotFoundOrWrongFormat, "type");
            ok = false;
            return shaders;
        }
        
        const auto type = typeFromString(typeString);

        if (type == gl::GL_NONE)
        {
            error(JsonParseError::ShaderTypeNotFound, typeString);
            ok = false;
            return shaders;
        }

        globjects::ref_ptr<globjects::AbstractStringSource> shaderFile = new globjects::File{fileName.toStdString()};

        const auto replacementsValue = shaderObject.value("replacements");
        
        if (!replacementsValue.isUndefined())
        {
            if (replacementsValue.isObject())
            {
                if (!replaceStrings(replacementsValue.toObject(), shaderFile))
                {
                    ok = false;
                    return shaders;
                }
            }
            else
            {
                error(JsonParseError::PropertyWrongFormat, "replacements");
                ok = false;
                return shaders;
            }
        }
        
        auto shader = globjects::make_ref<globjects::Shader>(type, shaderFile);
        
        if (!shader->compile())
        {
            m_compileFailures.push_back(fileName.toStdString());
            
            ok = false;
            return shaders;
        }

        shaders.push_back(globjects::ref_ptr<globjects::Shader>(shader));
    }
    
    ok = true;
    return shaders;
}

bool ShaderCompiler::replaceStrings(
    const QJsonObject & replacements,
    globjects::ref_ptr<globjects::AbstractStringSource> & stringSource)
{
    auto sourceTemplate = globjects::make_ref<globjects::StringTemplate>(stringSource);
    
    for (auto it = replacements.begin(); it != replacements.end(); ++it)
    {
        const auto valueString = it.value().toString();
        
        if (valueString.isNull())
        {
            error(JsonParseError::PropertyWrongFormat, it.key());
            return false;
        }
        
        sourceTemplate->replace(it.key().toStdString(), valueString.toStdString());
    }
    
    stringSource = sourceTemplate;
    return true;
}

gl::GLenum ShaderCompiler::typeFromString(const QString & typeString)
{
    if (typeString == "GL_VERTEX_SHADER")
    {
        return gl::GL_VERTEX_SHADER;
    }
    else if (typeString == "GL_VERTEX_SHADER")
    {
        return gl::GL_TESS_CONTROL_SHADER;
    }
    else if (typeString == "GL_TESS_EVALUATION_SHADER")
    {
        return gl::GL_TESS_EVALUATION_SHADER;
    }
    else if (typeString == "GL_GEOMETRY_SHADER")
    {
        return gl::GL_GEOMETRY_SHADER;
    }
    else if (typeString == "GL_FRAGMENT_SHADER")
    {
        return gl::GL_FRAGMENT_SHADER;
    }
    else if (typeString == "GL_COMPUTE_SHADER")
    {
        return gl::GL_COMPUTE_SHADER;
    }

    return gl::GL_NONE;
}

bool ShaderCompiler::createAndLinkProgram(
    const std::vector<globjects::ref_ptr<globjects::Shader>> & shaders)
{
    auto program = globjects::make_ref<globjects::Program>();

    for (auto & shader : shaders)
        program->attach(shader);

    program->link();

    if (!program->isLinked())
        return false;
    
    return true;
}

void ShaderCompiler::info(Info type)
{
    if (type == Info::Driver)
    {
        globjects::info() << "Driver: " << globjects::vendor();
        globjects::info() << "Renderer: " << globjects::renderer();
    }
    else if (type == Info::Failures)
    {
        if (!m_compileFailures.empty())
        {
            globjects::info();
            globjects::info() << "Compile Failures:";
            for (const auto failure : m_compileFailures)
                globjects::info() << "    " << failure;
        }
        
        if (!m_linkFailures.empty())
        {
            globjects::info();
            globjects::info() << "Link Failures:";
            for (const auto failure : m_linkFailures)
                globjects::info() << "    " << failure;
        }
    }   
}

void ShaderCompiler::error(JsonParseError error)
{
    m_errorLog.error(error);
}

void ShaderCompiler::error(JsonParseError::Type type, const QString & info)
{
    m_errorLog.error({ type, info });
}
