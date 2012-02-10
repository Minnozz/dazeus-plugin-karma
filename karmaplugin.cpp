/**
 * Copyright (c) Sjors Gielen, 2011-2012
 * See LICENSE for license.
 */

#include <QtCore/QList>
#include <QtCore/QDebug>
#include <QtCore/QCoreApplication>

#include "karmaplugin.h"

int main(int argc, char *argv[]) {
	if(argc < 2) {
		qWarning() << "Usage: dazeus-plugin-karma socketfile";
		return 1;
	}
	QString socketfile = argv[1];
	QCoreApplication app(argc, argv);
	KarmaPlugin kp(socketfile);
	return app.exec();
}

KarmaPlugin::KarmaPlugin(const QString &socketfile)
: QObject()
, d(new DaZeus())
{
	d->open(socketfile);
	d->subscribe("MESSAGE");
	connect(d,    SIGNAL(newEvent(DaZeus::Event*)),
	        this, SLOT(  newEvent(DaZeus::Event*)));
}

KarmaPlugin::~KarmaPlugin() {
	delete d;
}

int KarmaPlugin::modifyKarma(const QString &network, const QString &object, bool increase) {
	DaZeus::Scope s = DaZeus::Scope::Scope(network);
	QString qualifiedName = QLatin1String("perl.DazKarma.karma_") + object;
	int current = d->getProperty(qualifiedName, s).toInt();

	if(increase)
		++current;
	else	--current;

	if(current == 0) {
		d->unsetProperty(qualifiedName, s);
		// Also unset global property, in case one is left behind
		d->unsetProperty(qualifiedName, DaZeus::Scope::Scope());
	} else {
		d->setProperty(qualifiedName, QString::number(current), s);
	}

	return current;
}

void KarmaPlugin::newEvent(DaZeus::Event *e) {
	if(e->event != "MESSAGE") return;
	if(e->parameters.size() < 4) {
		qWarning() << "Incorrect parameter size for message received";
		return;
	}
	QString network = e->parameters[0];
	QString origin  = e->parameters[1];
	QString recv    = e->parameters[2];
	QString message = e->parameters[3];
	DaZeus::Scope s = DaZeus::Scope::Scope(network);
	// TODO: use getNick()
	if(!recv.startsWith('#')) {
		// reply to PM
		recv = origin;
	}
	if(message.startsWith("}karma ")) {
		QString object = message.mid(7).trimmed();
		int current = d->getProperty("perl.DazKarma.karma_" + object, s).toInt();
		if(current == 0) {
			d->message(network, recv, object + " has neutral karma.");
		} else {
			d->message(network, recv, object + " has a karma of " + QString::number(current) + ".");
		}
		return;
	}

	// Walk through the message searching for -- and ++; upon finding
	// either, reconstruct what the object was.
	// Start searching from i=1 because a string starting with -- or
	// ++ means nothing anyway, and now we can always ask for b[i-1]
	// End search at one character from the end so we can always ask
	// for b[i+1]
	QList<int> hits;
	int len = message.length();
	for(int i = 1; i < (len - 1); ++i) {
		bool wordEnd = i == len - 2 || message[i+2].isSpace();
		if( message[i] == QLatin1Char('-') && message[i+1] == QLatin1Char('-') && wordEnd ) {
			hits.append(i);
		}
		else if( message[i] == QLatin1Char('+') && message[i+1] == QLatin1Char('+') && wordEnd ) {
			hits.append(i);
		}
	}

	QListIterator<int> i(hits);
	while(i.hasNext()) {
		int pos = i.next();
		bool isIncrease = message[pos] == QLatin1Char('+');
		QString object;
		int newVal;

		if(message[pos-1].isLetter()) {
			// only alphanumerics between startPos and pos-1
			int startPos = pos - 1;
			for(; startPos >= 0; --startPos) {
				if(!message[startPos].isLetter()
				&& !message[startPos].isDigit()
				&& message[startPos] != QLatin1Char('-')
				&& message[startPos] != QLatin1Char('_'))
				{
					// revert the negation
					startPos++;
					break;
				}
			}
			if(startPos > 0 && !message[startPos-1].isSpace()) {
				// non-alphanumerics would be in this object, ignore it
				continue;
			}
			object = message.mid(startPos, pos - startPos);
			newVal = modifyKarma(network, object, isIncrease);
			qDebug() << origin << (isIncrease ? "increased" : "decreased")
			         << "karma of" << object << "to" << QString::number(newVal);
			continue;
		}

		char beginner;
		char ender;
		if(message[pos-1] == QLatin1Char(']')) {
			beginner = '[';
			ender = ']';
		} else if(message[pos-1] == QLatin1Char(')')) {
			beginner = '(';
			ender = ')';
		} else {
			continue;
		}

		// find the last $beginner before $ender
		int startPos = message.lastIndexOf(QLatin1Char(beginner), pos);
		// unless there's already an $ender between them
		if(message.indexOf(QLatin1Char(ender), startPos) < pos - 1)
			continue;

		object = message.mid(startPos + 1, pos - 2 - startPos);
		newVal = modifyKarma(network, object, isIncrease);
		QString message = origin + QLatin1String(isIncrease ? " increased" : " decreased")
		                + QLatin1String(" karma of ") + object + QLatin1String(" to ")
		                + QString::number(newVal) + QLatin1Char('.');
		qDebug() << message;
		if(ender == ']') {
			// Verbose mode, print the result
			d->message(network, recv, message);
		}
	}
}