Name:           gnome-system-log
Version:        3.9.90
Release:        3%{?dist}
Epoch:          1
Summary:        A log file viewer for GNOME

Group:          Applications/System
License:        GPLv2+ and GFDL
URL:            http://www.gnome.org
Source0:        http://download.gnome.org/sources/gnome-system-log/3.9/gnome-system-log-%{version}.tar.xz
Source1:        gnome-system-log
Source2:        org.gnome.logview.policy

BuildRequires: gtk3-devel
BuildRequires: intltool
BuildRequires: docbook-dtds
BuildRequires: desktop-file-utils
BuildRequires: itstool

Obsoletes: gnome-utils < 1:3.3
Obsoletes: gnome-utils-devel < 1:3.3
Obsoletes: gnome-utils-libs < 1:3.3

%description
gnome-system-log lets you view various log files on your system.

%prep
%setup -q


%build
%configure
make %{?_smp_mflags}


%install
make install DESTDIR=$RPM_BUILD_ROOT

desktop-file-validate $RPM_BUILD_ROOT%{_datadir}/applications/gnome-system-log.desktop

mv $RPM_BUILD_ROOT%{_bindir}/gnome-system-log $RPM_BUILD_ROOT%{_bindir}/logview
cp %{SOURCE1} $RPM_BUILD_ROOT%{_bindir}
chmod a+x $RPM_BUILD_ROOT%{_bindir}/gnome-system-log
mkdir -p $RPM_BUILD_ROOT%{_datadir}/polkit-1/actions
cp %{SOURCE2} $RPM_BUILD_ROOT%{_datadir}/polkit-1/actions

%find_lang %{name} --with-gnome

# https://bugzilla.redhat.com/show_bug.cgi?id=736523
#echo "%%dir %%{_datadir}/help/C" >> aisleriot.lang
#echo "%%{_datadir}/help/C/%%{name}" >> aisleriot.lang
#for l in ca cs de el en_GB es eu fi fr it ja ko oc ru sl sv uk zh_CN; do
#  echo "%%dir %%{_datadir}/help/$l"
#  echo "%%lang($l) %%{_datadir}/help/$l/%%{name}"
#done >> %{name}.lang

%post
for d in hicolor HighContrast ; do
  touch --no-create %{_datadir}/icons/$d >&/dev/null || :
done

%postun
if [ $1 -eq 0 ]; then
  glib-compile-schemas %{_datadir}/glib-2.0/schemas >&/dev/null || :
  for d in hicolor HighContrast ; do
    touch --no-create %{_datadir}/icons/$d >&/dev/null || :
    gtk-update-icon-cache %{_datadir}/icons/$d >&/dev/null || :
  done
fi

%posttrans
glib-compile-schemas %{_datadir}/glib-2.0/schemas >&/dev/null || :
for d in hicolor HighContrast ; do
  gtk-update-icon-cache %{_datadir}/icons/$d >&/dev/null || :
done

%files -f %{name}.lang
%doc COPYING COPYING.docs
%{_bindir}/gnome-system-log
%{_bindir}/logview
%{_datadir}/GConf/gsettings/logview.convert
%{_datadir}/applications/gnome-system-log.desktop
%{_datadir}/glib-2.0/schemas/org.gnome.gnome-system-log.gschema.xml
%{_datadir}/icons/hicolor/*/apps/logview.png
%{_datadir}/icons/HighContrast/*/apps/logview.png
%{_datadir}/polkit-1/actions/org.gnome.logview.policy
%doc %{_mandir}/man1/gnome-system-log.1.gz

%changelog
* Thu Mar 19 2015 Richard Hughes <rhughes@redhat.com> - 1:3.9.90-1
- Update to 3.9.90
- Resolves: #1174561

* Fri Jan 24 2014 Daniel Mach <dmach@redhat.com> - 1:3.8.1-5
- Mass rebuild 2014-01-24

* Fri Dec 27 2013 Daniel Mach <dmach@redhat.com> - 1:3.8.1-4
- Mass rebuild 2013-12-27

* Wed Dec 4 2013 Zeeshan Ali <zeenix@redhat.com> - 1:3.8.1-3
- Complete translations (related: #1030350).

* Wed Jul 10 2013 Matthias Clasen <mclasen@redhat.com> - 1:3.8.1-2
- Fix source url

* Mon Apr 15 2013 Kalev Lember <kalevlember@gmail.com> - 1:3.8.1-1
- Update to 3.8.1

* Tue Mar 26 2013 Kalev Lember <kalevlember@gmail.com> - 1:3.8.0-1
- Update to 3.8.0

* Tue Feb 19 2013 Richard Hughes <rhughes@redhat.com> - 1:3.7.90-1
- Update to 3.7.90

* Thu Feb 14 2013 Fedora Release Engineering <rel-eng@lists.fedoraproject.org> - 1:3.6.1-3
- Rebuilt for https://fedoraproject.org/wiki/Fedora_19_Mass_Rebuild

* Mon Nov 19 2012 Matthias Clasen <mclasen@redhat.com> - 1:3.6.1-2
- Use auth_admin instead of auth_self (#878115)

* Tue Nov 13 2012 Kalev Lember <kalevlember@gmail.com> - 1:3.6.1-1
- Update to 3.6.1
- Remove unused BRs

* Tue Sep 25 2012 Cosimo Cecchi <cosimoc@redhat.com> - 1:3.6.0-1
- Update to 3.6.0

* Wed Sep 19 2012 Richard Hughes <hughsient@gmail.com> - 1:3.5.92-1
- Update to 3.5.92

* Wed Sep 05 2012 Cosimo Cecchi <cosimoc@redhat.com> - 1:3.5.91-1
- Update to 3.5.91

* Tue Aug 21 2012 Richard Hughes <hughsient@gmail.com> - 1:3.5.90-1
- Update to 3.5.90

* Tue Jul 17 2012 Richard Hughes <hughsient@gmail.com> - 1:3.5.4-1
- Update to 3.5.4

* Tue Apr 24 2012 Kalev Lember <kalevlember@gmail.com> - 1:3.4.1-2
- Silence rpm scriptlet output

* Mon Apr 16 2012 Richard Hughes <hughsient@gmail.com> - 1:3.4.1-1
- Update to 3.4.1

* Thu Apr  5 2012 Matthias Clasen <mclasen@redhat.com> - 1:3.4.0-2
- Use pkexec to run privileged

* Mon Mar 26 2012 Cosimo Cecchi <cosimoc@redhat.com> - 1:3.4.0-1
- Update to 3.4.0

* Wed Mar 21 2012 Kalev Lember <kalevlember@gmail.com> - 1:3.3.92-1
- Update to 3.3.92

* Mon Mar 19 2012 Kalev Lember <kalevlember@gmail.com> - 1:3.3.1-4
- Use epoch to fix the upgrade path from the old gnome-system-log package that
  was built as part of gnome-utils

* Sat Mar 17 2012 Matthias Clasen <mclasen@redhat.com> - 3.3.1-3
- Obsolete gnome-utils and subpackages

* Fri Jan 13 2012 Fedora Release Engineering <rel-eng@lists.fedoraproject.org> - 3.3.1-2
- Rebuilt for https://fedoraproject.org/wiki/Fedora_17_Mass_Rebuild

* Fri Nov 18 2011  Matthias Clasen <mclasen@redhat.com> - 3.3.1-1
- Initial packaging
