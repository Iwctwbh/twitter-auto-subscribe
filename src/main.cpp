#include <fstream>
#include <QApplication>
#include <QWebEngineView>
#include <QWebEnginePage>
#include <QTimer>
#include <QObject>
#include <QNetworkProxy>
#include <QWebEngineProfile>
#include <QWebEngineCookieStore>
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <QDateTime>
#include <ranges>
#include <filesystem>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <QFile>
#include <QDir>

#pragma execution_character_set("utf-8")

std::string sanitize_filename(const std::string& s) {
	std::string result = s;
	for (char& c : result) {
		if (c == ':' || c == ' ' || c == '/' || c == '\\' ||
			c == '*' || c == '?' || c == '"' || c == '<' ||
			c == '>' || c == '|')
			c = '_';
	}
	return result;
}

void post_message(const nlohmann::json& config, const nlohmann::json& json_payload) {
	cpr::Response response = cpr::Post(
		cpr::Url{ config["push_url"].get<std::string>() },
		cpr::Header{ {"Content-Type", "application/json"} },
		cpr::Body{ json_payload.dump() }
	);
	if (response.status_code == 200) {
		qDebug() << "Response: " << response.text << "\n";
	} else {
		qDebug() << "Error: " << response.status_code << " - " << QString::fromLocal8Bit(response.error.message) << "\n";
	}
}

int main(int argc, char* argv[])
{
	QApplication a(argc, argv);
	qDebug() << "hello xmake\n";

	std::ifstream config_file("config.json");
	if (!config_file.is_open())
	{
		qDebug() << "Failed to open config.json";
		return -1;
	}

	nlohmann::json config;
	config_file >> config;

	if (!config.contains("proxy") || !config["proxy"].contains("host"))
	{
		qDebug() << "Invalid config: Missing proxy.host";
		return -1;
	}

	const auto& proxy_config = config["proxy"];
	const std::string proxy_host = proxy_config["host"].is_null() ? "" : proxy_config["host"].get<std::string>();

	if (proxy_host.empty())
	{
		qDebug() << "No proxy host provided, not using proxy.";
	}
	else
	{
		QNetworkProxy proxy;
		QString proxyType = proxy_config.contains("type") ? QString::fromUtf8(proxy_config["type"].get<std::string>()) : "";

		if (proxyType.isEmpty() || proxyType.compare("SOCKS5", Qt::CaseInsensitive) == 0)
		{
			proxy.setType(QNetworkProxy::Socks5Proxy);
		}
		else if (proxyType.compare("HTTP", Qt::CaseInsensitive) == 0)
		{
			proxy.setType(QNetworkProxy::HttpProxy);
		}
		else
		{
			qDebug() << "Invalid proxy type, defaulting to Socks5";
			proxy.setType(QNetworkProxy::Socks5Proxy);
		}

		proxy.setHostName(QString::fromUtf8(proxy_host));
		proxy.setPort(proxy_config["port"]);
		proxy.setUser(QString::fromUtf8(proxy_config["user"].get<std::string>()));
		proxy.setPassword(QString::fromUtf8(proxy_config["password"].get<std::string>()));
		QNetworkProxy::setApplicationProxy(proxy);

		qDebug() << "Using proxy:" << QString::fromUtf8(proxy_host);
	}

	QWebEngineView web_view;

	const QUrl url{ QString::fromUtf8(config["url"].get<std::string>()) };

	qputenv("QTWEBENGINE_CHROMIUM_FLAGS", "--disable-logging");
	qputenv("QTWEBENGINE_CHROMIUM_FLAGS", "--log-level=3");

	QWebEngineProfile* profile = web_view.page()->profile();
	QWebEngineCookieStore* cookie_store = profile->cookieStore();

	if (!config.contains("auth_token") || config["auth_token"].is_null() || config["auth_token"].get<std::string>().empty())
	{
		qDebug() << "Invalid config: Missing auth_token";
		return -1;
	}
	const std::string& auth_token = config["auth_token"].get<std::string>();

	QList<QNetworkCookie> cookies = {
		QNetworkCookie("auth_token", QByteArray::fromStdString(auth_token))
	};

	for (QNetworkCookie& cookie : cookies)
	{
		cookie.setDomain("x.com");
		cookie.setPath("/");
		cookie.setSecure(true);
		cookie_store->setCookie(cookie);
	}

	web_view.resize(960, 540);
	web_view.show();
	web_view.setZoomFactor(0.5);
	web_view.load(url);

	/*QWebEngineView dev_tools;
	dev_tools.resize(960, 540);
	web_view.page()->setDevToolsPage(dev_tools.page());
	dev_tools.show();*/

	QWebEnginePage* page = web_view.page();

	static QMetaObject::Connection timerConn;
	auto timer = new QTimer(&web_view);

	std::string last_time;
	{
		std::ifstream last_time_file("time.txt");
		if (last_time_file.is_open())
		{
			std::getline(last_time_file, last_time);
		}
		else
		{
			qDebug() << "Failed to open time.txt";
		}
	}

	QObject::connect(page, &QWebEnginePage::loadFinished, [&web_view, page, timer, config, &last_time](const bool ok)
		{
			if (!ok) {
				qDebug() << "loading failed";
				return;
			}
			qDebug() << "loading page";

			if (timerConn)
				QObject::disconnect(timerConn);

			timerConn = QObject::connect(timer, &QTimer::timeout, [page, timer, config, &last_time]()
				{
					std::string script =
							R"((function() {    const items = document.querySelectorAll("section div div div[data-testid='cellInnerDiv']");    if (items.length === 0) {        return null;    }    const formatTime = (timeStr) => {        const date = new Date(timeStr);        const pad = (num) => String(num).padStart(2, '0');        return `${date.getFullYear()}-${pad(date.getMonth() + 1)}-${pad(date.getDate())} ${pad(date.getHours())}:${pad(date.getMinutes())}:${pad(date.getSeconds())}`;    };    const getItemData = (item) => {        const time = item.querySelector("time")?.getAttribute("datetime");        const username = item.querySelector("span")?.textContent;        let content = item.querySelector("div[data-testid='tweetText']")?.textContent;        let type = "text";        if (!content || content.trim() === "") {            const videoPoster = item.querySelector("article img[alt='Ç¶ÈëÊ½ÊÓÆµ']")?.getAttribute("src");            if (videoPoster) {                content = videoPoster;                type = "video";            } else {                const imageSrc = item.querySelector("img")?.getAttribute("src");                if (imageSrc) {                    content = "";                    type = "image";                } else {                    content = "";                }            }        }        const rect = item.getBoundingClientRect();        if (time && username && rect) {            return {                time: formatTime(time),                username,                content,                type,                rect: {                    left: rect.left,                    top: rect.top,                    width: rect.width,                    height: rect.height                }            };        }        return null;    };    const results = [];    items.forEach(item => {        const data = getItemData(item);        if (data) {            results.push(data);        }    });    return results.length > 0 ? JSON.stringify(results) : null;})())";
						page->runJavaScript(QString::fromLocal8Bit(script.c_str(), script.size()),
						[timer, config, &last_time](const QVariant& result)
						{
							if (result.isNull()) {
								qDebug() << "JavaScript no return, retry";
								return;
							}
							qDebug() << "-------------------------------------------------------------------------------";
							qDebug() << "JavaScript return:" << result.toString().toUtf8().constData();
							qDebug() << "-------------------------------------------------------------------------------";

							nlohmann::json current_result = nlohmann::json::parse(result.toString().toUtf8().constData(), nullptr, false);

							if (!current_result.is_array())
								return;

							std::vector<nlohmann::json> filtered_msgs;
							for (const auto& msg : current_result)
							{
								if (!msg.contains("time")) continue;
								const std::string& msg_time = msg["time"].get<std::string>();
								if (msg_time > last_time)
									filtered_msgs.push_back(msg);
							}

							std::ranges::sort(filtered_msgs,
								[](const nlohmann::json& a, const nlohmann::json& b)
								{
									return a["time"].get<std::string>() > b["time"].get<std::string>();
								});

							for (auto& msg : std::ranges::reverse_view(filtered_msgs))
							{
								qDebug() << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")
									<< " "
									<< "JavaScript return:"
									<< msg["time"].get<std::string>()
									<< msg["username"].get<std::string>()
									<< msg["content"].get<std::string>();

								nlohmann::json json_payload;
								const std::string& type = msg["type"].get<std::string>();

								if (type == "video")
								{
									const std::string& url = msg["content"].get<std::string>();
									if (!url.empty())
									{
										qDebug() << "download_url: " << url;
										std::filesystem::create_directories("download");
										std::string filename = "download/" + sanitize_filename(msg["time"].get<std::string>() + "_" + msg["username"].get<std::string>() + ".jpg");

										QNetworkAccessManager manager;
										QNetworkRequest request(QUrl(QString::fromStdString(url)));
										QNetworkReply* reply = manager.get(request);

										QEventLoop loop;
										QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
										loop.exec();

										if (reply->error() == QNetworkReply::NoError)
										{
											QByteArray imageData = reply->readAll();
											QString base64String = imageData.toBase64();
											std::string base64StringStd = base64String.toLocal8Bit().constData();
											qDebug() << base64StringStd;

											json_payload["group_id"] = config["qq_group_id"];
											json_payload["message"] = {
												{
													{"type", "text"},
													{"data", {{"text", msg["time"].get<std::string>() + "\n" + msg["username"].get<std::string>()}}}
												}
											};
											post_message(config, json_payload);

											json_payload["message"] = {
												{
													{"type", "image"},
													{"data", {{"file", "base64://" + base64StringStd}}}
												}
											};
											post_message(config, json_payload);
										}
										else
										{
											qDebug() << "Failed to download:" << QString::fromStdString(url) << reply->errorString();
										}
										reply->deleteLater();
									}
								}
								else
								{
									json_payload["group_id"] = config["qq_group_id"];
									json_payload["message"] = {
										{
											{"type", "text"},
											{"data", {{"text", msg["time"].get<std::string>() + "\n" + msg["username"].get<std::string>() + "\n" + msg["content"].get<std::string>()}}}
										}
									};
									post_message(config, json_payload);
								}

								last_time = msg["time"].get<std::string>();
								std::ofstream out_time("time.txt");
								if (out_time.is_open())
									out_time << last_time;
							}
							timer->stop();
						});
				});
			timer->start(5000);
		});

	auto refresh_timer = new QTimer(&web_view);
	QObject::connect(refresh_timer, &QTimer::timeout, [&web_view, url]()
		{
			qDebug() << "Refreshing page";
			QWebEngineProfile* profile = web_view.page()->profile();
			QWebEngineCookieStore* cookie_store = profile->cookieStore();

			static QMetaObject::Connection cookieConn;
			if (cookieConn)
				QObject::disconnect(cookieConn);

			cookie_store->loadAllCookies();
			cookieConn = QObject::connect(cookie_store, &QWebEngineCookieStore::cookieAdded,
				[](const QNetworkCookie& cookie)
				{
					qDebug() << "Cookie:" << cookie.toRawForm();
				}
			);

			web_view.load(url);
		});
	refresh_timer->start(60000);

	auto keep_alive_timer = new QTimer(&web_view);
	QObject::connect(keep_alive_timer, &QTimer::timeout, [&config]()
		{
			qDebug() << "Keep alive request";
			nlohmann::json json_payload;
			json_payload["group_id"] = config["qq_group_id"];
			json_payload["message"] = {
				{
					{"type", "text"},
					{"data", {{"text", "Keep alive test"}}}
				}
			};
			post_message(config, json_payload);
		});
	keep_alive_timer->start(600000);

	return QApplication::exec();
}
