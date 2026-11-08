#include "FtpSession.h"
#include "StringUtils.h"
#include <cstdarg>
#include <strings.h>

namespace
{
    using Kind = FtpProtocol::Kind;
    using Status = FtpProtocol::Status;
    using Entry = FtpProtocol::Entry;

    constexpr const char *Features = "211-Features:\r\n EPSV\r\n MDTM\r\n MLST type*;size*;modify*;\r\n PASV\r\n REST STREAM\r\n SIZE\r\n TVFS\r\n UTF8\r\n211 End\r\n";
    constexpr const char *Help = "214-The following commands are recognized:\r\n ABOR ALLO CDUP CWD DELE EPSV FEAT HELP LIST MDTM MKD MLSD MLST MODE NLST NOOP OPTS PASS PASV PWD QUIT REST RETR RMD RNFR RNTO SIZE STAT STOR STRU SYST TYPE USER\r\n214 Help OK.\r\n";
}

const FtpSession::Command FtpSession::Commands[] = {
    {"USER", false, &FtpSession::user},
    {"PASS", false, &FtpSession::pass},
    {"QUIT", false, &FtpSession::quit},
    {"FEAT", false, &FtpSession::feat},
    {"OPTS", false, &FtpSession::opts},
    {"NOOP", false, &FtpSession::noop},
    {"SYST", false, &FtpSession::syst},
    {"HELP", false, &FtpSession::help},
    {"PWD", true, &FtpSession::pwd},
    {"XPWD", true, &FtpSession::pwd},
    {"CWD", true, &FtpSession::cwd},
    {"XCWD", true, &FtpSession::cwd},
    {"CDUP", true, &FtpSession::cdup},
    {"XCUP", true, &FtpSession::cdup},
    {"TYPE", true, &FtpSession::type},
    {"MODE", true, &FtpSession::mode},
    {"STRU", true, &FtpSession::stru},
    {"PASV", true, &FtpSession::pasv},
    {"EPSV", true, &FtpSession::epsv},
    {"PORT", true, &FtpSession::port},
    {"EPRT", true, &FtpSession::port},
    {"LIST", true, &FtpSession::listUnix},
    {"NLST", true, &FtpSession::listNames},
    {"MLSD", true, &FtpSession::listFacts},
    {"MLST", true, &FtpSession::mlst},
    {"SIZE", true, &FtpSession::size},
    {"MDTM", true, &FtpSession::mdtm},
    {"REST", true, &FtpSession::rest},
    {"RETR", true, &FtpSession::retr},
    {"STOR", true, &FtpSession::stor},
    {"DELE", true, &FtpSession::dele},
    {"RMD", true, &FtpSession::rmd},
    {"XRMD", true, &FtpSession::rmd},
    {"MKD", true, &FtpSession::mkd},
    {"XMKD", true, &FtpSession::mkd},
    {"RNFR", true, &FtpSession::rnfr},
    {"RNTO", true, &FtpSession::rnto},
    {"ABOR", true, &FtpSession::abor},
    {"STAT", true, &FtpSession::stat},
    {"ALLO", true, &FtpSession::allo},
};

FtpSession::FtpSession(FtpEnvironment &environment, FtpProtocol::Job &job, char *text, size_t textCapacity) : m_env(environment), m_job(job), m_text(text), m_textCapacity(textCapacity)
{
}

void FtpSession::start()
{
    strcpy(m_cwd, "/");
    m_renameFrom[0] = 0;
    m_renamePending = m_userGiven = m_userValid = m_loggedIn = m_passive = false;
    m_failures = 0;
    m_rest = 0;
    m_started = m_lastActivity = m_env.now();
    m_transfer = Transfer::None;
    m_bytes = 0;
    m_textUsed = 0;
    m_hasDeferred = m_deferredInvalid = false;
    reply("220 RetroRadioTab5 FTP ready.\r\n");
}

bool FtpSession::command(const char *line)
{
    // Eigene Kopie: Die Zeile der Umgebung wird während einer Übertragung vom nächsten Befehl überschrieben.
    stringutils::safeCopy(m_line, line);
    bool open = execute(m_line);
    while (open && (m_hasDeferred || m_deferredInvalid))
    {
        if (m_deferredInvalid)
        {
            m_deferredInvalid = false;
            rejectLine();
            continue;
        }
        memcpy(m_line, m_deferred, sizeof(m_line));
        FtpProtocol::wipe(m_deferred, sizeof(m_deferred));
        m_hasDeferred = false;
        open = execute(m_line);
    }
    // Die Zeile kann ein Passwort enthalten.
    FtpProtocol::wipe(m_line, sizeof(m_line));
    return open;
}

void FtpSession::rejectLine()
{
    m_lastActivity = m_env.now();
    reply("500 Line too long or invalid.\r\n");
}

bool FtpSession::tick()
{
    const uint64_t now = m_env.now();
    if (!m_loggedIn && now - m_started >= FtpProtocol::LoginTimeoutMs)
    {
        close("421 Login timeout.\r\n");
        return false;
    }
    if (now - m_lastActivity >= FtpProtocol::IdleTimeoutMs)
    {
        close("421 Idle timeout, closing control connection.\r\n");
        return false;
    }
    return true;
}

void FtpSession::close(const char *text)
{
    m_env.closeData();
    m_passive = false;
    m_loggedIn = false;
    reply(text);
}

bool FtpSession::execute(const char *line)
{
    m_lastActivity = m_env.now();
    char verb[5];
    const char *argument = "";
    if (!FtpProtocol::splitCommand(line, verb, argument))
        return reply("500 Syntax error, command unrecognized.\r\n");
    const bool renaming = m_renamePending;
    m_renamePending = false;
    for (const auto &command : Commands)
    {
        if (strcmp(command.verb, verb))
            continue;
        if (command.login && !m_loggedIn)
            return reply("530 Please login with USER and PASS.\r\n");
        if (command.handler == &FtpSession::rnto && !renaming)
            return reply("503 RNFR required first.\r\n");
        return (this->*command.handler)(argument);
    }
    return reply("502 Command not implemented.\r\n");
}

bool FtpSession::reply(const char *text)
{
    m_env.reply(text);
    return true;
}

bool FtpSession::replyf(const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(m_reply, sizeof(m_reply), format, arguments);
    va_end(arguments);
    m_env.reply(m_reply);
    return true;
}

bool FtpSession::resolve(const char *argument, char (&out)[FtpProtocol::MaxPath + 1]) const
{
    return FtpProtocol::resolvePath(m_cwd, argument, out);
}

bool FtpSession::acquire()
{
    const uint64_t until = m_env.now() + FtpProtocol::LeaseWaitMs;
    for (;;)
    {
        const auto lease = m_env.lease();
        if (lease == FtpEnvironment::Lease::Granted)
            return true;
        // Das Webportal hält die Karte nur für die Dauer einer Anfrage oder eines Uploads, kurz darauf ist sie meist frei.
        if (lease == FtpEnvironment::Lease::Busy && m_env.now() < until)
        {
            m_env.wait(FtpProtocol::LeaseRetryMs);
            continue;
        }
        if (lease == FtpEnvironment::Lease::Playing)
            reply("450 SD card busy: SD playback is active.\r\n");
        else if (lease == FtpEnvironment::Lease::Busy)
            reply("450 SD card busy: web file manager is active.\r\n");
        else
            reply("450 SD card busy, try again.\r\n");
        return false;
    }
}

bool FtpSession::run(Kind kind, const char *path, const char *target)
{
    if (!acquire())
        return false;
    m_job.kind = kind;
    stringutils::safeCopy(m_job.path, path);
    stringutils::safeCopy(m_job.target, target ? target : "");
    m_env.file(m_job);
    m_env.release();
    return true;
}

bool FtpSession::fail(Status status)
{
    switch (status)
    {
    case Status::NotFound:
        return reply("550 No such file or directory.\r\n");
    case Status::Exists:
        return reply("550 File or directory already exists.\r\n");
    case Status::Denied:
        return reply("550 Permission denied.\r\n");
    case Status::IsDirectory:
        return reply("550 Is a directory.\r\n");
    case Status::NotDirectory:
        return reply("550 Not a directory.\r\n");
    case Status::Invalid:
        return reply("553 File name not allowed.\r\n");
    case Status::NoSpace:
        return reply("452 Insufficient storage space on the SD card.\r\n");
    case Status::Busy:
        return reply("450 SD card busy: SD playback is active.\r\n");
    case Status::NoCard:
        return reply("451 SD card not available.\r\n");
    default:
        return reply("451 SD card read or write error.\r\n");
    }
}

FtpSession::Outcome FtpSession::control()
{
    // Liegt schon eine Zeile bereit, wird erst nach der Übertragung weitergelesen.
    if (m_hasDeferred || m_deferredInvalid)
        return Outcome::Done;
    switch (m_env.poll(m_deferred))
    {
    case FtpEnvironment::Control::None:
        return Outcome::Done;
    case FtpEnvironment::Control::Closed:
        return Outcome::Lost;
    case FtpEnvironment::Control::Invalid:
        m_deferredInvalid = true;
        return Outcome::Done;
    case FtpEnvironment::Control::Line:
        break;
    }
    char verb[5];
    const char *argument = "";
    if (FtpProtocol::splitCommand(m_deferred, verb, argument) && (!strcmp(verb, "ABOR") || !strcmp(verb, "QUIT")))
    {
        const bool quit = verb[0] == 'Q';
        FtpProtocol::wipe(m_deferred, sizeof(m_deferred));
        return quit ? Outcome::Quit : Outcome::Aborted;
    }
    m_hasDeferred = true;
    return Outcome::Done;
}

FtpSession::Outcome FtpSession::outcome(FtpEnvironment::Data data)
{
    switch (data)
    {
    case FtpEnvironment::Data::Ok:
    case FtpEnvironment::Data::Control:
        return Outcome::Done;
    case FtpEnvironment::Data::Closed:
        return Outcome::DataClosed;
    case FtpEnvironment::Data::Timeout:
        return Outcome::DataTimeout;
    case FtpEnvironment::Data::Memory:
        return Outcome::Memory;
    case FtpEnvironment::Data::Radio:
        return Outcome::Radio;
    default:
        return Outcome::Shutdown;
    }
}

FtpSession::Outcome FtpSession::transmit(const uint8_t *data, size_t length)
{
    for (size_t done = 0; done < length;)
    {
        size_t sent = 0;
        const auto result = m_env.send(data + done, length - done, sent, interruptible());
        done += sent;
        m_bytes += sent;
        if (result != FtpEnvironment::Data::Control)
            return outcome(result);
        // Ein Client, der den Datenkanal nicht mehr liest, schickt ABOR. Ohne diese Prüfung käme es erst nach der Frist an.
        const Outcome checked = control();
        if (checked != Outcome::Done)
            return checked;
    }
    return Outcome::Done;
}

FtpSession::Outcome FtpSession::flush()
{
    if (!m_textUsed)
        return Outcome::Done;
    const Outcome result = transmit(reinterpret_cast<const uint8_t *>(m_text), m_textUsed);
    m_textUsed = 0;
    return result;
}

FtpSession::Outcome FtpSession::emit(const char *text, size_t length)
{
    if (m_textUsed + length > m_textCapacity)
    {
        const Outcome sent = flush();
        if (sent != Outcome::Done)
            return sent;
    }
    if (length > m_textCapacity)
        return Outcome::Done;
    memcpy(m_text + m_textUsed, text, length);
    m_textUsed += length;
    return Outcome::Done;
}

bool FtpSession::finish(Outcome result, Kind close, Status card, const char *command, const char *path)
{
    m_job.kind = close;
    m_env.file(m_job);
    m_env.closeData();
    m_passive = false;
    m_env.release();
    m_transfer = Transfer::None;
    m_textUsed = 0;
    const char *text = "226 Transfer complete.\r\n";
    bool open = true;
    switch (result)
    {
    case Outcome::Done:
        ++m_transfers;
        break;
    case Outcome::Aborted:
        text = "426 Transfer aborted.\r\n226 ABOR successful.\r\n";
        break;
    case Outcome::Quit:
        text = "426 Transfer aborted.\r\n221 Goodbye.\r\n";
        open = false;
        break;
    case Outcome::Lost:
        text = nullptr;
        open = false;
        break;
    case Outcome::DataClosed:
        text = "426 Data connection closed, transfer aborted.\r\n";
        break;
    case Outcome::DataTimeout:
        text = "426 Data connection timed out, transfer aborted.\r\n";
        break;
    case Outcome::Shutdown:
        text = "421 Service shutting down.\r\n";
        open = false;
        break;
    case Outcome::Memory:
        text = "451 Not enough free memory on the device, transfer aborted.\r\n";
        break;
    case Outcome::Radio:
        text = "451 Radio stream started on the device, transfer aborted.\r\n";
        break;
    case Outcome::Card:
        if (card == Status::NoSpace)
            text = "452 Insufficient storage space on the SD card, transfer aborted.\r\n";
        else if (card == Status::NoCard)
            text = "451 SD card not available, transfer aborted.\r\n";
        else if (card == Status::IsDirectory)
            text = "553 Cannot replace a directory, transfer aborted.\r\n";
        else
            text = "451 SD card read or write error, transfer aborted.\r\n";
        break;
    }
    char first[80] = "control connection lost";
    if (text)
    {
        const size_t length = static_cast<size_t>(strstr(text, "\r\n") - text);
        memcpy(first, text, length < sizeof(first) ? length : sizeof(first) - 1);
        first[length < sizeof(first) ? length : sizeof(first) - 1] = 0;
        reply(text);
    }
    m_env.transferred(command, path, first, m_bytes);
    return open;
}

bool FtpSession::user(const char *argument)
{
    m_loggedIn = false;
    m_rest = 0;
    m_userGiven = argument[0] != 0;
    m_userValid = !strcmp(argument, FtpProtocol::User);
    // Die Antwort verrät nicht, ob der Name stimmt.
    return reply(m_userGiven ? "331 Password required.\r\n" : "501 User name required.\r\n");
}

bool FtpSession::pass(const char *argument)
{
    if (m_loggedIn)
        return reply("503 Already logged in.\r\n");
    if (!m_userGiven)
        return reply("503 Login with USER first.\r\n");
    m_userGiven = false;
    const uint64_t now = m_env.now();
    if (const uint64_t wait = m_lock.remaining(now))
        return replyf("530 Too many failed logins, try again in %lu s.\r\n", static_cast<unsigned long>((wait + 999) / 1000));
    char secret[FtpProtocol::PasswordCapacity]{};
    const bool valid = m_env.password(secret) && m_userValid && FtpProtocol::equalSecret(argument, secret);
    FtpProtocol::wipe(secret, sizeof(secret));
    if (valid)
    {
        // Anders als im Webportal gilt nach Erfolg kein Mindestabstand: Clients bauen nach einer Trennung sofort neu auf.
        m_lock = WebValidation::LoginLock{};
        m_loggedIn = true;
        m_failures = 0;
        strcpy(m_cwd, "/");
        return reply("230 Login successful.\r\n");
    }
    m_lock.failed(now);
    ++m_failures;
    ++m_failedLogins;
    reply("530 Login incorrect.\r\n");
    return m_failures < FtpProtocol::MaxLoginFailures;
}

bool FtpSession::quit(const char *)
{
    reply("221 Goodbye.\r\n");
    return false;
}

bool FtpSession::feat(const char *)
{
    return reply(Features);
}

bool FtpSession::opts(const char *argument)
{
    if (!strcasecmp(argument, "UTF8 ON") || !strcasecmp(argument, "UTF8") || !strcasecmp(argument, "UTF-8 ON"))
        return reply("200 Always in UTF8 mode.\r\n");
    if (!strncasecmp(argument, "MLST", 4) && (!argument[4] || argument[4] == ' '))
        return reply("200 MLST OPTS type;size;modify;\r\n");
    return reply("501 Option not understood.\r\n");
}

bool FtpSession::noop(const char *)
{
    return reply("200 NOOP ok.\r\n");
}

bool FtpSession::syst(const char *)
{
    return reply("215 UNIX Type: L8\r\n");
}

bool FtpSession::help(const char *)
{
    return reply(Help);
}

bool FtpSession::pwd(const char *)
{
    return replyf("257 \"%s\" is the current directory.\r\n", m_cwd);
}

bool FtpSession::cwd(const char *argument)
{
    if (!argument[0])
        return reply("501 Missing directory name.\r\n");
    char path[FtpProtocol::MaxPath + 1];
    if (!resolve(argument, path))
        return reply("550 Invalid path.\r\n");
    if (strcmp(path, "/"))
    {
        if (!run(Kind::Stat, path))
            return true;
        if (m_job.status != Status::Ok)
            return fail(m_job.status);
        if (!m_job.entry.directory)
            return reply("550 Not a directory.\r\n");
    }
    stringutils::safeCopy(m_cwd, path);
    return reply("250 Directory successfully changed.\r\n");
}

bool FtpSession::cdup(const char *)
{
    char path[FtpProtocol::MaxPath + 1];
    if (resolve("..", path))
        stringutils::safeCopy(m_cwd, path);
    return reply("250 Directory successfully changed.\r\n");
}

bool FtpSession::type(const char *argument)
{
    const char kind = argument[0] >= 'a' ? static_cast<char>(argument[0] - 32) : argument[0];
    if ((kind == 'A' && (!argument[1] || !strcasecmp(argument + 1, " N"))) || (kind == 'I' && !argument[1]) || !strcasecmp(argument, "L 8"))
        return replyf("200 Type set to %c.\r\n", kind == 'L' ? 'I' : kind);
    return reply("504 Type not supported.\r\n");
}

bool FtpSession::mode(const char *argument)
{
    return reply(!strcasecmp(argument, "S") ? "200 Mode set to S.\r\n" : "504 Only stream mode is supported.\r\n");
}

bool FtpSession::stru(const char *argument)
{
    return reply(!strcasecmp(argument, "F") ? "200 Structure set to F.\r\n" : "504 Only file structure is supported.\r\n");
}

bool FtpSession::pasv(const char *)
{
    uint8_t ip[4]{};
    uint16_t number = 0;
    char text[64];
    m_passive = m_env.passive(ip, number) && FtpProtocol::passiveReply(ip, number, text, sizeof(text));
    return reply(m_passive ? text : "425 Cannot open passive connection.\r\n");
}

bool FtpSession::epsv(const char *argument)
{
    if (!strcasecmp(argument, "ALL"))
        return reply("200 EPSV ALL ok.\r\n");
    if (argument[0] && strcmp(argument, "1"))
        return reply("522 Network protocol not supported, use (1).\r\n");
    uint8_t ip[4]{};
    uint16_t number = 0;
    char text[64];
    m_passive = m_env.passive(ip, number) && FtpProtocol::extendedPassiveReply(number, text, sizeof(text));
    return reply(m_passive ? text : "425 Cannot open passive connection.\r\n");
}

bool FtpSession::port(const char *)
{
    return reply("502 Active mode not supported, use PASV or EPSV.\r\n");
}

bool FtpSession::listUnix(const char *argument)
{
    return list(argument, Format::List);
}

bool FtpSession::listNames(const char *argument)
{
    return list(argument, Format::Names);
}

bool FtpSession::listFacts(const char *argument)
{
    return list(argument, Format::Facts);
}

bool FtpSession::list(const char *argument, Format format)
{
    m_rest = 0;
    // Optionen wie „-la“ von LIST und NLST zeigen hier nichts anderes.
    if (format != Format::Facts)
        while (*argument == '-')
        {
            while (*argument && *argument != ' ')
                ++argument;
            while (*argument == ' ')
                ++argument;
        }
    char path[FtpProtocol::MaxPath + 1];
    if (!resolve(argument, path))
        return reply("550 Invalid path.\r\n");
    if (!m_passive)
        return reply("425 Use PASV or EPSV first.\r\n");
    if (!acquire())
        return true;
    m_job.kind = Kind::Stat;
    stringutils::safeCopy(m_job.path, path);
    m_env.file(m_job);
    const bool directory = m_job.status == Status::Ok && m_job.entry.directory;
    if (m_job.status == Status::Ok && !directory && format == Format::Facts)
    {
        m_env.release();
        return reply("501 Not a directory.\r\n");
    }
    if (directory)
    {
        m_job.kind = Kind::ListOpen;
        m_env.file(m_job);
    }
    if (m_job.status != Status::Ok)
    {
        m_env.release();
        return fail(m_job.status);
    }
    reply(directory ? "150 Here comes the directory listing.\r\n" : "150 Opening data connection.\r\n");
    if (!m_env.accept())
    {
        m_job.kind = Kind::ListClose;
        m_env.file(m_job);
        m_env.closeData();
        m_passive = false;
        m_env.release();
        return reply("425 Cannot open data connection.\r\n");
    }
    m_transfer = Transfer::List;
    m_bytes = 0;
    m_textUsed = 0;
    const int64_t now = m_env.wallClock();
    char line[FtpProtocol::MaxPath + 96];
    auto put = [&](const Entry &entry)
    {
        size_t length = 0;
        if (format == Format::List)
            length = FtpProtocol::listLine(entry, entry.name, now, line, sizeof(line));
        else if (format == Format::Facts)
            length = FtpProtocol::factsLine(entry, entry.name, false, line, sizeof(line));
        else
        {
            const int n = snprintf(line, sizeof(line), "%s\r\n", entry.name);
            length = n > 0 && static_cast<size_t>(n) < sizeof(line) ? static_cast<size_t>(n) : 0;
        }
        return length ? emit(line, length) : Outcome::Done;
    };
    Outcome result = Outcome::Done;
    if (!directory)
        result = put(m_job.entry);
    else
        for (;;)
        {
            m_job.kind = Kind::ListNext;
            m_env.file(m_job);
            if (m_job.status != Status::Ok && m_job.status != Status::End)
            {
                result = Outcome::Card;
                break;
            }
            for (size_t i = 0; i < m_job.entryCount && result == Outcome::Done; ++i)
                result = put(m_job.entries[i]);
            if (result != Outcome::Done || m_job.status == Status::End)
                break;
            result = control();
            if (result != Outcome::Done)
                break;
        }
    const Status card = m_job.status;
    if (result == Outcome::Done)
        result = flush();
    const char *name = format == Format::List ? "LIST" : "MLSD";
    if (format == Format::Names)
        name = "NLST";
    return finish(result, Kind::ListClose, card, name, path);
}

bool FtpSession::mlst(const char *argument)
{
    char path[FtpProtocol::MaxPath + 1];
    if (!resolve(argument, path))
        return reply("550 Invalid path.\r\n");
    if (!run(Kind::Stat, path))
        return true;
    if (m_job.status != Status::Ok)
        return fail(m_job.status);
    char facts[FtpProtocol::MaxPath + 96];
    if (!FtpProtocol::factsLine(m_job.entry, path, true, facts, sizeof(facts)))
        return reply("451 Local error.\r\n");
    replyf("250-Listing %s\r\n", path);
    reply(facts);
    return reply("250 End.\r\n");
}

bool FtpSession::size(const char *argument)
{
    if (!argument[0])
        return reply("501 Missing file or directory name.\r\n");
    char path[FtpProtocol::MaxPath + 1];
    if (!resolve(argument, path))
        return reply("550 Invalid path.\r\n");
    if (!run(Kind::Stat, path))
        return true;
    if (m_job.status != Status::Ok)
        return fail(m_job.status);
    if (m_job.entry.directory)
        return reply("550 Not a regular file.\r\n");
    return replyf("213 %llu\r\n", static_cast<unsigned long long>(m_job.entry.size));
}

bool FtpSession::mdtm(const char *argument)
{
    if (!argument[0])
        return reply("501 Missing file or directory name.\r\n");
    char path[FtpProtocol::MaxPath + 1];
    if (!resolve(argument, path))
        return reply("550 Invalid path.\r\n");
    if (!run(Kind::Stat, path))
        return true;
    if (m_job.status != Status::Ok)
        return fail(m_job.status);
    char stamp[15];
    if (!FtpProtocol::timestamp(m_job.entry.modified, stamp))
        return reply("550 Modification time unknown.\r\n");
    return replyf("213 %s\r\n", stamp);
}

bool FtpSession::rest(const char *argument)
{
    uint64_t offset = 0;
    if (!FtpProtocol::parseOffset(argument, offset))
        return reply("501 Invalid REST parameter.\r\n");
    m_rest = offset;
    return replyf("350 Restarting at %llu. Send RETR to resume.\r\n", static_cast<unsigned long long>(offset));
}

bool FtpSession::retr(const char *argument)
{
    const uint64_t offset = m_rest;
    m_rest = 0;
    if (!argument[0])
        return reply("501 Missing file name.\r\n");
    char path[FtpProtocol::MaxPath + 1];
    if (!resolve(argument, path))
        return reply("550 Invalid path.\r\n");
    if (!m_passive)
        return reply("425 Use PASV or EPSV first.\r\n");
    if (!acquire())
        return true;
    m_job.kind = Kind::ReadOpen;
    stringutils::safeCopy(m_job.path, path);
    m_job.offset = offset;
    m_env.file(m_job);
    if (m_job.status != Status::Ok)
    {
        m_env.release();
        if (m_job.status == Status::Invalid && offset)
            return reply("554 Invalid REST offset.\r\n");
        if (m_job.status == Status::IsDirectory)
            return reply("550 Not a regular file.\r\n");
        return fail(m_job.status);
    }
    replyf("150 Opening BINARY mode data connection for %s (%llu bytes).\r\n", path, static_cast<unsigned long long>(m_job.entry.size - offset));
    if (!m_env.accept())
    {
        m_job.kind = Kind::Close;
        m_env.file(m_job);
        m_env.closeData();
        m_passive = false;
        m_env.release();
        return reply("425 Cannot open data connection.\r\n");
    }
    m_transfer = Transfer::Retr;
    m_bytes = 0;
    Outcome result = Outcome::Done;
    for (;;)
    {
        m_job.kind = Kind::Read;
        m_env.file(m_job);
        if (m_job.status == Status::End)
            break;
        if (m_job.status != Status::Ok)
        {
            result = Outcome::Card;
            break;
        }
        result = transmit(m_job.data, m_job.count);
        if (result != Outcome::Done)
            break;
        result = control();
        if (result != Outcome::Done)
            break;
    }
    return finish(result, Kind::Close, m_job.status, "RETR", path);
}

bool FtpSession::stor(const char *argument)
{
    const uint64_t offset = m_rest;
    m_rest = 0;
    if (!argument[0])
        return reply("501 Missing file name.\r\n");
    char path[FtpProtocol::MaxPath + 1];
    if (!resolve(argument, path) || !strcmp(path, "/"))
        return reply("553 File name not allowed.\r\n");
    // Ein Upload ersetzt das Ziel erst nach vollständigem Empfang, ein Fortsetzen ab einem Versatz müsste den alten Anfang erst kopieren.
    if (offset)
        return reply("554 Resuming uploads is not supported.\r\n");
    if (!m_passive)
        return reply("425 Use PASV or EPSV first.\r\n");
    const auto upload = m_env.uploadAllowed();
    if (upload == FtpEnvironment::Upload::Radio)
        return reply("450 Radio stream is playing on the device. Stop it for uploads.\r\n");
    if (upload == FtpEnvironment::Upload::Memory)
        return reply("452 Not enough free memory on the device for uploads, try again later.\r\n");
    if (!acquire())
        return true;
    m_job.kind = Kind::WriteOpen;
    stringutils::safeCopy(m_job.path, path);
    m_env.file(m_job);
    if (m_job.status != Status::Ok)
    {
        m_env.release();
        if (m_job.status == Status::NotFound)
            return reply("553 Directory not found.\r\n");
        if (m_job.status == Status::IsDirectory)
            return reply("553 Is a directory.\r\n");
        return fail(m_job.status);
    }
    reply("150 Ok to send data.\r\n");
    if (!m_env.accept())
    {
        m_job.kind = Kind::Close;
        m_env.file(m_job);
        m_env.closeData();
        m_passive = false;
        m_env.release();
        return reply("425 Cannot open data connection.\r\n");
    }
    m_transfer = Transfer::Stor;
    m_bytes = 0;
    Outcome result = Outcome::Done;
    for (;;)
    {
        size_t received = 0;
        const auto data = m_env.receive(m_job.data, m_job.capacity < FtpProtocol::UploadBlock ? m_job.capacity : FtpProtocol::UploadBlock, received, interruptible());
        result = outcome(data);
        if (result != Outcome::Done)
            break;
        if (received)
        {
            if (m_bytes + received > FtpProtocol::MaxFileSize)
            {
                m_job.status = Status::NoSpace;
                result = Outcome::Card;
                break;
            }
            m_job.kind = Kind::Write;
            m_job.count = static_cast<uint32_t>(received);
            m_env.file(m_job);
            if (m_job.status != Status::Ok)
            {
                result = Outcome::Card;
                break;
            }
            m_bytes += received;
        }
        result = control();
        if (result != Outcome::Done || (data == FtpEnvironment::Data::Ok && !received))
            break;
    }
    // Ein abbrechender Client schließt den Datenkanal oft kurz vor ABOR oder der Trennung des Steuerkanals, nur ohne beides gilt der Upload als vollständig.
    const uint64_t until = m_env.now() + FtpProtocol::UploadSettleMs;
    while (result == Outcome::Done && !m_hasDeferred && !m_deferredInvalid && m_env.now() < until)
    {
        result = control();
        if (result == Outcome::Done)
            m_env.wait(10);
    }
    if (result == Outcome::Done)
    {
        m_job.kind = Kind::Commit;
        m_env.file(m_job);
        if (m_job.status != Status::Ok)
            result = Outcome::Card;
    }
    return finish(result, Kind::Close, m_job.status, "STOR", path);
}

bool FtpSession::dele(const char *argument)
{
    if (!argument[0])
        return reply("501 Missing file or directory name.\r\n");
    char path[FtpProtocol::MaxPath + 1];
    if (!resolve(argument, path) || !strcmp(path, "/"))
        return reply("550 Invalid path.\r\n");
    if (!run(Kind::Delete, path))
        return true;
    if (m_job.status == Status::IsDirectory)
        return reply("550 Is a directory, use RMD.\r\n");
    return m_job.status == Status::Ok ? reply("250 File deleted.\r\n") : fail(m_job.status);
}

bool FtpSession::rmd(const char *argument)
{
    if (!argument[0])
        return reply("501 Missing file or directory name.\r\n");
    char path[FtpProtocol::MaxPath + 1];
    if (!resolve(argument, path))
        return reply("550 Invalid path.\r\n");
    if (!strcmp(path, "/"))
        return reply("550 Cannot remove the root directory.\r\n");
    if (!acquire())
        return true;
    m_job.kind = Kind::RmdirHidden;
    stringutils::safeCopy(m_job.path, path);
    m_job.target[0] = 0;
    m_env.file(m_job);
    // Enthält der Ordner nur versteckte Einträge, entfernt der Dienstworker sie in Schritten. Die Kartensperre bleibt bis zum Ende gehalten.
    const bool hiddenOnly = m_job.status == Status::More;
    while (m_job.status == Status::More)
    {
        m_job.kind = Kind::TreeStep;
        m_env.file(m_job);
    }
    const Status status = m_job.status == Status::End ? Status::Ok : m_job.status;
    if (hiddenOnly && status != Status::Ok)
    {
        // Busy oder NoCard erreichen den Kartenspeicher nicht, die Baumarbeit wird ohne Kartenzugriff beendet.
        m_job.kind = Kind::TreeClose;
        m_env.file(m_job);
    }
    m_env.release();
    if (!hiddenOnly && status == Status::Denied)
        return reply("550 Directory not empty.\r\n");
    if (status != Status::Ok)
        return fail(status);
    // Ein gelöschter Arbeitsordner wäre danach nicht mehr erreichbar.
    char parentPath[FtpProtocol::MaxPath + 1];
    if (!strcmp(m_cwd, path) && resolve("..", parentPath))
        stringutils::safeCopy(m_cwd, parentPath);
    return reply("250 Directory removed.\r\n");
}

bool FtpSession::mkd(const char *argument)
{
    if (!argument[0])
        return reply("501 Missing directory name.\r\n");
    char path[FtpProtocol::MaxPath + 1];
    if (!resolve(argument, path))
        return reply("553 File name not allowed.\r\n");
    if (!run(Kind::Mkdir, path))
        return true;
    if (m_job.status == Status::Exists)
        return reply("550 Directory already exists.\r\n");
    if (m_job.status == Status::NotFound)
        return reply("550 Parent directory not found.\r\n");
    if (m_job.status != Status::Ok)
        return fail(m_job.status);
    return replyf("257 \"%s\" created.\r\n", path);
}

bool FtpSession::rnfr(const char *argument)
{
    if (!argument[0])
        return reply("501 Missing file or directory name.\r\n");
    char path[FtpProtocol::MaxPath + 1];
    if (!resolve(argument, path))
        return reply("550 Invalid path.\r\n");
    if (!strcmp(path, "/"))
        return reply("550 Cannot rename the root directory.\r\n");
    if (!run(Kind::Stat, path))
        return true;
    if (m_job.status != Status::Ok)
        return fail(m_job.status);
    stringutils::safeCopy(m_renameFrom, path);
    m_renamePending = true;
    return reply("350 Ready for RNTO.\r\n");
}

bool FtpSession::rnto(const char *argument)
{
    if (!argument[0])
        return reply("501 Missing file or directory name.\r\n");
    char path[FtpProtocol::MaxPath + 1];
    if (!resolve(argument, path) || !strcmp(path, "/"))
        return reply("553 File name not allowed.\r\n");
    if (!run(Kind::Rename, m_renameFrom, path))
        return true;
    if (m_job.status == Status::Exists)
        return reply("550 Target already exists.\r\n");
    if (m_job.status == Status::Invalid)
        return reply("553 Rename not allowed.\r\n");
    if (m_job.status != Status::Ok)
        return fail(m_job.status);
    // Lag der Arbeitsordner im umbenannten Ordner, wandert er mit.
    const size_t length = strlen(m_renameFrom);
    if (!strncmp(m_cwd, m_renameFrom, length) && (!m_cwd[length] || m_cwd[length] == '/'))
    {
        char moved[FtpProtocol::MaxPath + 1];
        const int n = snprintf(moved, sizeof(moved), "%s%s", path, m_cwd + length);
        stringutils::safeCopy(m_cwd, n > 0 && static_cast<size_t>(n) < sizeof(moved) && WebValidation::path(moved) ? moved : "/");
    }
    return reply("250 Rename successful.\r\n");
}

bool FtpSession::abor(const char *)
{
    m_env.closeData();
    m_passive = false;
    return reply("225 No transfer to abort.\r\n");
}

bool FtpSession::stat(const char *argument)
{
    if (argument[0])
        return reply("504 STAT with arguments not supported.\r\n");
    return reply("211-RetroRadioTab5 FTP status:\r\n Logged in as tab5\r\n TYPE: BINARY, STRUcture: File, MODE: Stream\r\n211 End of status.\r\n");
}

bool FtpSession::allo(const char *)
{
    return reply("202 No storage allocation necessary.\r\n");
}
