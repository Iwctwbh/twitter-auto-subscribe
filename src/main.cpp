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

#pragma execution_character_set("utf-8")

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

	if (config["proxy"]["host"].is_null() || config["proxy"]["host"].get<std::string>().empty())
	{
		qDebug() << "No proxy host provided, not using proxy.";
	}
	else
	{
		QNetworkProxy proxy;

		QString proxyType = QString::fromUtf8(config["proxy"]["type"].get<std::string>());

		if (proxyType.isEmpty())
		{
			qDebug() << "Proxy type is empty, defaulting to Socks5";
			proxy.setType(QNetworkProxy::Socks5Proxy);
		}
		else
		{
			if (proxyType.compare("HTTP", Qt::CaseInsensitive) == 0)
			{
				proxy.setType(QNetworkProxy::HttpProxy);
			}
			else if (proxyType.compare("SOCKS5", Qt::CaseInsensitive) == 0)
			{
				proxy.setType(QNetworkProxy::Socks5Proxy);
			}
			else
			{
				qDebug() << "Invalid proxy type, defaulting to Socks5";
				proxy.setType(QNetworkProxy::Socks5Proxy);
			}
		}

		proxy.setHostName(QString::fromUtf8(config["proxy"]["host"].get<std::string>()));
		proxy.setPort(config["proxy"]["port"]);
		proxy.setUser(QString::fromUtf8(config["proxy"]["user"].get<std::string>()));
		proxy.setPassword(QString::fromUtf8(config["proxy"]["password"].get<std::string>()));
		QNetworkProxy::setApplicationProxy(proxy);

		qDebug() << "Using proxy:" << QString::fromUtf8(config["proxy"]["host"].get<std::string>());
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
	std::string auth_token = config["auth_token"].get<std::string>();

	QList<QNetworkCookie> cookies =
	{
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
	web_view.load(url);

	QWebEnginePage* page = web_view.page();

	static QMetaObject::Connection timerConn;
	auto timer = new QTimer(&web_view);

	static nlohmann::json last_result;

	QObject::connect(page, &QWebEnginePage::loadFinished, [&web_view, page, timer, config](const bool ok)
		{
			if (ok)
			{
				qDebug() << "loading page";

				if (timerConn)
				{
					QObject::disconnect(timerConn);
				}

				timerConn = QObject::connect(timer, &QTimer::timeout, [page, timer, config]()
					{
						page->runJavaScript(
							R"((function() {    const items = document.querySelectorAll("section div div div[data-testid='cellInnerDiv']");    const parseTime = (timeStr) => new Date(timeStr).getTime();    const formatTime = (timeStr) => {        const date = new Date(timeStr);        const pad = (num) => String(num).padStart(2, '0');        return `${date.getFullYear()}-${pad(date.getMonth() + 1)}-${pad(date.getDate())} ${pad(date.getHours())}:${pad(date.getMinutes())}:${pad(date.getSeconds())}`;    };    const getItemData = (item) => {        if (!item) return null;        const time = item.querySelector("time")?.getAttribute("datetime");        const username = item.querySelector("span")?.textContent;        const content = item.querySelector("div[data-testid='tweetText']")?.textContent;        if (time) {            return { time: formatTime(time), username, content };        }        return null;    };    const firstItem = getItemData(items[0]);    const secondItem = getItemData(items[1]);    if (firstItem && secondItem) {        return JSON.stringify(            parseTime(firstItem.time) > parseTime(secondItem.time) ? firstItem : secondItem        );    } else if (firstItem) {        return JSON.stringify(firstItem);    } else if (secondItem) {        return JSON.stringify(secondItem);    } else {        return null;    }})())",
							[timer, config](const QVariant& result)
							{
								if (!result.isNull())
								{
									qDebug() <<
										"-------------------------------------------------------------------------------";
									qDebug() << "JavaScript return:" << result.toString().toUtf8().constData();
									qDebug() <<
										"-------------------------------------------------------------------------------";

									nlohmann::json current_result = nlohmann::json::parse(
										result.toString().toUtf8().constData(), nullptr, false);

									if (current_result != last_result)
									{
										qDebug() << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")
											<< " "
											<< "JavaScript return:"
											<< current_result["time"].get<std::string>()
											<< current_result["username"].get<std::string>()
											<< current_result["content"].get<std::string>();
										last_result = current_result;

										nlohmann::json json_payload = config["json_payload"];
										json_payload["message"][0]["data"]["text"] =
											current_result["time"].get<std::string>()
											+ "\n"
											+ current_result["username"].get<std::string>()
											+ "\n"
											+ current_result["content"].get<std::string>();

										cpr::Response response = cpr::Post(
											cpr::Url{ "http://192.168.137.1:3000/send_group_msg" },
											cpr::Header{ {"Content-Type", "application/json"} },
											cpr::Body{ json_payload.dump() }
										);

										response.status_code == 200
											? qDebug() << "Response: " << response.text << "\n"
											: qDebug() << "Error: " << response.status_code << " - " << response.error.message
											<< "\n";
									}
									else
									{
										qDebug() << "JavaScript return is the same, skipping";
									}

									timer->stop();
								}
								else
								{
									qDebug() << "JavaScript no return, retry";
								}
							});
					});

				timer->start(5000);
			}
			else
			{
				qDebug() << "loading failed";
			}
		});

	auto refresh_timer = new QTimer(&web_view);
	QObject::connect(refresh_timer, &QTimer::timeout, [&web_view, url]()
		{
			qDebug() << "Refreshing page";
			QWebEngineProfile* profile = web_view.page()->profile();
			QWebEngineCookieStore* cookie_store = profile->cookieStore();

			static QMetaObject::Connection cookieConn;
			if (cookieConn)
			{
				QObject::disconnect(cookieConn);
			}

			cookie_store->loadAllCookies();
			cookieConn = QObject::connect(cookie_store, &QWebEngineCookieStore::cookieAdded,
				[](const QNetworkCookie& cookie)
				{
					qDebug() << "Cookie:" << cookie.toRawForm();
				}
			);

			web_view.load(url);
		});
	refresh_timer->start(120000);

	auto keep_alive_timer = new QTimer(&web_view);
	QObject::connect(keep_alive_timer, &QTimer::timeout, [&web_view, url, &config]()
		{
			qDebug() << "Keep alive request";
			nlohmann::json json_payload = config["json_payload"];
			json_payload["message"][0]["data"]["text"] = "Keep alive test";
			cpr::Response response = cpr::Post(
				cpr::Url{ "http://192.168.137.1:3000/send_group_msg" },
				cpr::Header{ {"Content-Type", "application/json"} },
				cpr::Body{ json_payload.dump() }
			);
			response.status_code == 200
				? qDebug() << "Response: " << response.text << "\n"
				: qDebug() << "Error: " << response.status_code << " - " << response.error.message
				<< "\n";
		});
	keep_alive_timer->start(600000);

	return QApplication::exec();
}
