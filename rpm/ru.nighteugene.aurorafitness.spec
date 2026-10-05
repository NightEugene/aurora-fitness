Name: ru.nighteugene.aurorafitness
Summary: Фитнес-клиент для Mi Band и PineTime (BLE)
Version: 1.1.0
Release: 1
License: BSD-3-Clause
Source0: %{name}-%{version}.tar.bz2
BuildRequires: pkgconfig(auroraapp)
BuildRequires: pkgconfig(Qt5Core)
BuildRequires: pkgconfig(Qt5Qml)
BuildRequires: pkgconfig(Qt5Quick)
BuildRequires: pkgconfig(Qt5Gui)
BuildRequires: pkgconfig(Qt5DBus)
BuildRequires: pkgconfig(Qt5Sql)
BuildRequires: pkgconfig(openssl)
BuildRequires: pkgconfig(dbus-1)
Requires: sailfishsilica-qt5 >= 0.10.9

%description
Фитнес-приложение для ОС Аврора с поддержкой носимых устройств
по Bluetooth Low Energy.
Подключение к Mi Band 8 и PineTime с InfiniTime через BlueZ D-Bus API.

%prep
%autosetup

%build
%qmake5
%make_build

%install
%make_install

%files
%defattr(-,root,root,-)
%{_bindir}/%{name}
%defattr(644,root,root,-)
%{_datadir}/%{name}
%{_datadir}/applications/%{name}.desktop
%{_datadir}/icons/hicolor/*/apps/%{name}.png
